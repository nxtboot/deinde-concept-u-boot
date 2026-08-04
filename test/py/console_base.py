# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2015 Stephen Warren
# Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.

"""Common logic to interact with U-Boot via the console.

Provides the interface that tests use to execute U-Boot shell commands and wait
for their results. Sub-classes exist to perform board-type-specific setup
operations, such as spawning a sub-process for Sandbox, or attaching to the
serial console of real hardware.
"""

from collections import namedtuple
import re
import sys
import time
import pytest

import spawn

# Regexes for text we expect U-Boot to send to the console.
pattern_u_boot_spl_signon = re.compile('(Deinde Concept U-Boot SPL \\d{4}\\.\\d{2}[^\r\n]*\\))')
pattern_u_boot_main_signon = re.compile('(Deinde Concept U-Boot \\d{4}\\.\\d{2}[^\r\n]*\\))')
pattern_stop_autoboot_prompt = re.compile('Hit any key to stop autoboot: ')
pattern_unknown_command = re.compile('Unknown command \'.*\' - try \'help\'')
pattern_error_notification = re.compile('## Error: ')
pattern_error_please_reset = re.compile('### ERROR ### Please RESET the board ###')
pattern_ready_prompt = re.compile('{lab ready in (.*)s: (.*)}')
pattern_lab_mode = re.compile('{lab mode.*}')

# Timeout before expecting the console to be ready (in milliseconds)
TIMEOUT_MS = 30000                  # Standard timeout
TIMEOUT_CMD_MS = 10000              # Command-echo timeout

# Maximum bytes to read at once from the console. 4KB matches the typical
# Linux PTY buffer size (N_TTY_BUF_SIZE), so there's no benefit to larger.
RECV_BUF_SIZE = 4096

# Default timeout for board preparation in lab mode. This needs to be enough to
# build U-Boot, write it to the board and then boot the board. Since this
# process is under the control of another program (e.g. Labgrid), it will fail
# sooner if something goes wrong. So use a very long timeout here to cover all
# possible situations. A board that can need slow recovery (e.g. an Edison
# reflashed over its BootROM with xfstk) can raise this for itself via the
# 'prepare_timeout' role setting, picked up by ubconfig.prepare_timeout.
TIMEOUT_PREPARE_MS = 3 * 60 * 1000

# Named pattern used by this module:
#    str: name of pattern
#    re.Pattern: Regex to check this pattern in the console output
NamedPattern = namedtuple('PATTERN', 'name,pattern')

# Named patterns we can look for in the console output. These can indicate an
# error has occurred
PATTERNS = (
    NamedPattern('spl_signon', pattern_u_boot_spl_signon),
    NamedPattern('main_signon', pattern_u_boot_main_signon),
    NamedPattern('stop_autoboot_prompt', pattern_stop_autoboot_prompt),
    NamedPattern('unknown_command', pattern_unknown_command),
    NamedPattern('error_notification', pattern_error_notification),
    NamedPattern('error_please_reset', pattern_error_please_reset),
)


class Timeout(Exception):
    """An exception sub-class that indicates that a timeout occurred."""


class BootFail(Exception):
    """An exception sub-class that indicates that a boot failure occurred.

    This is used when a bad pattern is seen when waiting for the boot prompt.
    It is regarded as fatal, to avoid trying to boot the again and again to no
    avail.
    """


class Unexpected(Exception):
    """An exception sub-class that indicates that unexpected test was seen."""


def handle_exception(ubconfig, console, log, err, name, fatal, output=''):
    """Handle an exception from the console

    Exceptions can occur when there is unexpected output or due to the board
    crashing or hanging. Some exceptions are likely fatal, where retrying will
    just chew up time to no available. In those cases it is best to cause
    further tests be skipped.

    Args:
        ubconfig (ArbitraryAttributeContainer): ubconfig object
        log (Logfile): Place to log errors
        console (ConsoleBase): Console to clean up, if fatal
        err (Exception): Exception which was thrown
        name (str): Name of problem, to log
        fatal (bool): True to abort all tests
        output (str): Extra output to report on boot failure. This can show the
           target's console output as it tried to boot
    """
    msg = f'{name}: '
    if fatal:
        msg += 'Marking connection bad - no other tests will run'
    else:
        msg += 'Assuming that lab is healthy'
    print(msg)
    log.error(msg)
    log.error(f'Error: {err}')

    if output:
        msg += f'; output {output}'

    if fatal:
        ubconfig.connection_ok = False
        console.cleanup_spawn()
        pytest.exit(msg)


class ConsoleDisableCheck():
    """Context manager (for Python's with statement) that temporarily disables
    the specified console output error check. This is useful when deliberately
    executing a command that is known to trigger one of the error checks, in
    order to test that the error condition is actually raised. This class is
    used internally by ConsoleBase::disable_check(); it is not intended for
    direct usage."""

    def __init__(self, console, check_type):
        self.console = console
        self.check_type = check_type

    def __enter__(self):
        self.console.disable_check_count[self.check_type] += 1
        self.console.eval_patterns()

    def __exit__(self, extype, value, traceback):
        self.console.disable_check_count[self.check_type] -= 1
        self.console.eval_patterns()


class ConsoleEnableCheck():
    """Context manager (for Python's with statement) that temporarily enables
    the specified console output error check. This is useful when executing a
    command that might raise an extra bad pattern, beyond the default bad
    patterns, in order to validate that the extra bad pattern is actually
    detected. This class is used internally by ConsoleBase::enable_check(); it
    is not intended for direct usage."""

    def __init__(self, console, check_type, check_pattern):
        self.console = console
        self.check_type = check_type
        self.check_pattern = check_pattern
        self.default_bad_patterns = None

    def __enter__(self):
        cons = self.console
        self.default_bad_patterns = cons.avail_patterns
        cons.avail_patterns.append((self.check_type, self.check_pattern))
        cons.disable_check_count = {pat.name: 0 for pat in PATTERNS}
        cons.eval_patterns()

    def __exit__(self, extype, value, traceback):
        cons = self.console
        cons.avail_patterns = self.default_bad_patterns
        cons.disable_check_count = {pat.name: 0 for pat in PATTERNS}
        cons.eval_patterns()


class ConsoleSetupTimeout():
    """Context manager (for Python's with statement) that temporarily sets up
    timeout for specific command. This is useful when execution time is greater
    then default 30s."""

    def __init__(self, console, timeout):
        self.console = console
        self.orig_timeout = self.console.timeout
        self.console.timeout = timeout

    def __enter__(self):
        return self

    def __exit__(self, extype, value, traceback):
        self.console.timeout = self.orig_timeout


class ConsoleBase():
    """The interface through which test functions interact with the U-Boot
    console. This primarily involves executing shell commands, capturing their
    results, and checking for common error conditions. Some common utilities
    are also provided too."""

    def __init__(self, log, config, max_fifo_fill):
        """Initialize a U-Boot console connection.

        Can only usefully be called by sub-classes.

        Args:
            log (multiplexed_log.Logfile): Log to which the U-Boot output is
                logged.
            config (ArbitraryAttributeContainer): ubman_fix.config, as built by
                conftest.py.
            max_fifo_fill (int): The max number of characters to send to U-Boot
                command-line before waiting for U-Boot to echo the characters
                back. For UART-based HW without HW flow control, this value
                should be set less than the UART RX FIFO size to avoid
                overflow, assuming that U-Boot can't keep up with full-rate
                traffic at the baud rate.

        Properties:
            logstream (LogfileStream): Log stream being used
            prompt (str): Prompt string expected from U-Boot
            p (spawn.Spawn): Means of communicating with running U-Boot via a
                console
            avail_patterns (list of NamedPattern): Normally the same as
                PATTERNS but can be adjusted by tests
            disable_check_count: dict of 'nest counts' for patterns
                key (str): NamedPattern.name
                value (int): 0 if not disabled, >0 for the number of 'requests
                    to disable' that have been received for this pattern
            at_prompt (bool): True if the running U-Boot is at a prompt and
                thus ready to receive commands
            at_prompt_logevt (int): Logstream event number when the prompt was
                detected. This is used to avoid logging the prompt twice
            lab_mode (bool): True if the lab is responsible for getting U-Boot
                to a prompt, i.e. able to process commands on the console
            u_boot_version_string (str): Version string obtained from U-Boot as
                it booted. In lab mode this is provided by
                pattern_ready_prompt
            buf (str): Buffer of characters received from the console, still to
                be processed
            output (str); All data received from the console
            before (str): Data before the matching string
            after (str): String which patches the expected output
            timeout (str): Timeout in seconds before giving up and aborting the
                test
            logfile_read (multiplexed_log.Logfile): Logfile used for logging
                output
            re_vt100 (re.Regex): Regex for filtering out vt100 characters
        """
        self.log = log
        self.config = config
        self.max_fifo_fill = max_fifo_fill

        self.logstream = self.log.get_stream('console', sys.stdout)

        # Array slice removes leading/trailing quotes
        self.prompt = self.config.buildconfig['config_sys_prompt'][1:-1]
        self.prompt_compiled = re.compile('^' + re.escape(self.prompt), re.MULTILINE)
        self.p = None
        self.avail_patterns = PATTERNS
        self.disable_check_count = {pat.name: 0 for pat in self.avail_patterns}
        self.at_prompt = False
        self.at_prompt_logevt = None
        self.lab_mode = False
        self.u_boot_version_string = None
        self.prepare_for_spawn()
        # http://stackoverflow.com/questions/7857352/python-regex-to-match-vt100-escape-sequences
        self.re_vt100 = re.compile(r'(\x1b\[|\x9b)[^@-_]*[@-_]|\x1b[@-_]', re.I)

        self.eval_patterns()

    def prepare_for_spawn(self):
        """Reset all settings as we are about to spawn a new connection"""
        self.buf = ''
        self.output = ''
        self.before = ''
        self.after = ''
        self.timeout = None
        self.logfile_read = None

    def get_spawn(self):
        # This is not called, ssubclass must define this.
        # Return a value to avoid:
        #   console_base.py:348:12: E1128: Assigning result of a function
        #   call, where the function returns None (assignment-from-none)
        return spawn.Spawn([])

    def get_default_timeout(self):
        """Get the default timeout for commands.

        Subclasses can override this to provide a different timeout.
        For example, sandbox may need a longer timeout when mcheck is enabled.

        Returns:
            int: Timeout in milliseconds, or None if timeout is disabled
        """
        if self.config.gdbserver or self.config.no_timeout:
            return None
        return TIMEOUT_MS

    def eval_patterns(self):
        """Set up lists of regexes for patterns we don't expect on console"""
        self.bad_patterns = [pat.pattern for pat in self.avail_patterns
                             if not self.disable_check_count[pat.name]]
        self.bad_pattern_ids = [pat.name for pat in self.avail_patterns
                                if not self.disable_check_count[pat.name]]

    def close(self):
        """Terminate the connection to the U-Boot console.

        This function is only useful once all interaction with U-Boot is
        complete. Once this function is called, data cannot be sent to or
        received from U-Boot.

        Args:
            None.
        """
        if self.p:
            self.log.start_section('Stopping U-Boot')
            close_type = self.p.close()
            self.log.info(f'Close type: {close_type}')
            self.log.end_section('Stopping U-Boot')
        self.logstream.close()

    def _prepare_timeout_ms(self):
        """Return the lab-mode board-preparation timeout in milliseconds

        A board that can need slow recovery may override the default
        (TIMEOUT_PREPARE_MS) by providing a 'prepare_timeout' in seconds via
        its lab role, surfaced as ubconfig.prepare_timeout.

        Returns:
            int: Timeout in milliseconds
        """
        secs = getattr(self.config, 'prepare_timeout', None)
        return secs * 1000 if secs else TIMEOUT_PREPARE_MS

    def set_lab_mode(self):
        """Select lab mode

        This tells us that we will get a 'lab ready' message when the board is
        ready for use. We don't need to look for signon messages.
        """
        self.log.info('test.py: Lab mode is active')
        self.timeout = self._prepare_timeout_ms()
        self.lab_mode = True

    def _wait_for_boot_prompt(self, loop_num=1, lab_marker_expected=True):
        """Wait for the boot up until command prompt.

        This is for internal use only.

        Args:
            loop_num (int): Number of banner occurrences to wait for.
            lab_marker_expected (bool): In lab mode, True means the labgrid
                reset wrapper emits the '{lab ready ...}' marker that must
                be consumed. False means this is an in-test reboot (reset
                command, EFI watchdog, ...) with no labgrid involvement,
                so match the plain '=>' prompt instead.
        """
        try:
            self.log.info('Waiting for U-Boot to be ready')

            if self.lab_mode:
                self.timeout = self._prepare_timeout_ms()
            if not self.lab_mode:
                self._wait_for_banner(loop_num)
                self.u_boot_version_string = self.after
            # In lab mode the '{lab ready in <t>s: <banner>}' marker is the
            # authoritative ready signal and must be consumed so the
            # embedded banner does not trip the signon bad-pattern on the
            # next command. For in-test reboots there is no labgrid
            # involvement, so fall back to matching the plain '=>' prompt.
            match_prompt = not self.lab_mode or not lab_marker_expected
            if match_prompt:
                wait_patterns = [self.prompt_compiled, pattern_ready_prompt,
                                 pattern_stop_autoboot_prompt]
            else:
                wait_patterns = [pattern_ready_prompt,
                                 pattern_stop_autoboot_prompt]
            ready_idx = wait_patterns.index(pattern_ready_prompt)
            autoboot_idx = wait_patterns.index(pattern_stop_autoboot_prompt)
            base = len(wait_patterns)
            while True:
                m = self.expect(wait_patterns + self.bad_patterns)
                if match_prompt and m == 0:
                    self.log.info(f'Found ready prompt {m}')
                    break
                if m == ready_idx:
                    m = pattern_ready_prompt.search(self.after)
                    self.u_boot_version_string = m.group(2)
                    self.log.info('Lab: Board is ready')
                    self.timeout = self.get_default_timeout()
                    break
                if m == autoboot_idx:
                    self.log.info(f'Found autoboot prompt {m}')
                    self.p.send(' ')
                    continue
                if not self.lab_mode:
                    raise BootFail('Missing prompt / ready message on console: ' +
                                   self.bad_pattern_ids[m - base])
            self.log.info('U-Boot is ready')

        finally:
            self.log.timestamp()

    def _wait_for_banner(self, loop_num):
        """Wait for a U-Boot banner to appear on the console

        Args:
            loop_num (int): Number of times to expect a banner (used for when
                U-Boot is expected to start up and then reset itself)
        """
        bcfg = self.config.buildconfig
        config_spl_serial = bcfg.get('config_spl_serial', 'n') == 'y'
        env_spl_skipped = self.config.env.get('env__spl_skipped', False)
        env_spl_banner_times = self.config.env.get('env__spl_banner_times', 1)
        while loop_num > 0:
            loop_num -= 1
            while config_spl_serial and not env_spl_skipped and env_spl_banner_times > 0:
                m = self.expect([pattern_u_boot_spl_signon,
                                   pattern_lab_mode] + self.bad_patterns)
                if m == 1:
                    self.set_lab_mode()
                    break
                if m != 0:
                    raise BootFail('Bad pattern found on SPL console: ' +
                                   self.bad_pattern_ids[m - 1])
                env_spl_banner_times -= 1

            if not self.lab_mode:
                m = self.expect([pattern_u_boot_main_signon,
                                   pattern_lab_mode] + self.bad_patterns)
                if m == 1:
                    self.set_lab_mode()
                elif m != 0:
                    raise BootFail('Bad pattern found on console: ' +
                                   self.bad_pattern_ids[m - 1])

    def run_command(self, cmd, wait_for_echo=True, send_nl=True,
                    wait_for_prompt=True, wait_for_reboot=False):
        """Execute a command via the U-Boot console.

        The command is always sent to U-Boot.

        U-Boot echoes any command back to its output, and this function
        typically waits for that to occur. The wait can be disabled by setting
        wait_for_echo=False, which is useful e.g. when sending CTRL-C to
        interrupt a long-running command such as "ums".

        Command execution is typically triggered by sending a newline
        character. This can be disabled by setting send_nl=False, which is
        also useful when sending CTRL-C.

        This function typically waits for the command to finish executing, and
        returns the console output that it generated. This can be disabled by
        setting wait_for_prompt=False, which is useful when invoking a long-
        running command such as "ums".

        Args:
            cmd (str): The command to send.
            wait_for_echo (bool): Indicates whether to wait for U-Boot to
                echo the command text back to its output.
            send_nl (bool): Indicates whether to send a newline character
                after the command string.
            wait_for_prompt (bool): Indicates whether to wait for the
                command prompt to be sent by U-Boot. This typically occurs
                immediately after the command has been executed.
            wait_for_reboot (bool): Indicates whether to wait U-Boot ro reboot.
                If True, wait_for_prompt must also be True.

        Returns:
            If wait_for_prompt == False:
                Empty string.
            Else:
                The output from U-Boot during command execution. In other
                words, the text U-Boot emitted between the point it echod the
                command string and emitted the subsequent command prompts.
        """
        if self.at_prompt and \
                self.at_prompt_logevt != self.logstream.logfile.cur_evt:
            self.logstream.write(self.prompt, implicit=True)

        try:
            self.at_prompt = False
            if not self.p:
                raise BootFail(
                    f"Lab failure: Connection lost when sending command '{cmd}'")

            if send_nl:
                cmd += '\n'
            rem = cmd  # Remaining to be sent
            with self.temporary_timeout(TIMEOUT_CMD_MS):
                while rem:
                    # Limit max outstanding data, so UART FIFOs don't overflow
                    chunk = rem[:self.max_fifo_fill]
                    rem = rem[self.max_fifo_fill:]
                    self.p.send(chunk)
                    if not wait_for_echo:
                        continue
                    chunk = re.escape(chunk)
                    chunk = chunk.replace('\\\n', '[\r\n]')
                    m = self.expect([chunk] + self.bad_patterns)
                    if m != 0:
                        self.at_prompt = False
                        raise BootFail('Failed to get echo on console '
                                       f"(cmd '{cmd}':rem '{rem}'): " +
                                       self.bad_pattern_ids[m - 1])
            if not wait_for_prompt:
                return ''
            if wait_for_reboot:
                self._wait_for_boot_prompt(lab_marker_expected=False)
            else:
                m = self.expect([self.prompt_compiled] + self.bad_patterns)
                if m != 0:
                    self.at_prompt = False
                    raise BootFail('Missing prompt on console: ' +
                                    self.bad_pattern_ids[m - 1])
            self.at_prompt = True
            self.at_prompt_logevt = self.logstream.logfile.cur_evt
            # Only strip \r\n; space/TAB might be significant if testing
            # indentation.
            return self.before.strip('\r\n')
        except Timeout as exc:
            handle_exception(self.config, self, self.log, exc,
                             f"Lab failure: Timeout executing '{cmd}'", True)
            raise
        except BootFail as exc:
            handle_exception(self.config, self, self.log, exc,
                             f"'Boot fail '{cmd}'",
                             True, self.get_spawn_output())
            raise
        finally:
            self.log.timestamp()
        return ''

    def run_command_list(self, cmds):
        """Run a list of commands.

        This is a helper function to call run_command() with default arguments
        for each command in a list.

        Args:
            cmds (list of str): List of commands
        Returns:
            A list of output strings from each command, one element for each
            command.
        """
        output = []
        for cmd in cmds:
            output.append(self.run_command(cmd))
        return output

    def run_ut(self, suite, test, **kwargs):
        """Run a manual unit test

        Run a unit test that has the _norun suffix, meaning it requires
        external setup (like creating a disk image) before it can run.

        Args:
            suite (str): Test suite name (e.g., 'fs')
            test (str): Test name without _norun suffix
                (e.g., 'fs_test_ext4l_probe')
            **kwargs: Test arguments passed as key=value
                (e.g., fs_image='/path/to/img')

        Returns:
            str: Command output

        Raises:
            AssertionError: If test reports failures
        """
        args = ' '.join(f'{k}={v}' for k, v in kwargs.items())
        cmd = f'ut -f {suite} {test}_norun'
        if args:
            cmd += f' {args}'
        output = self.run_command(cmd)
        assert 'failures: 0' in output, f'Test {test} failed'
        return output

    def send(self, msg):
        """Send characters without waiting for echo, etc."""
        self.run_command(msg, wait_for_prompt=False, wait_for_echo=False,
                         send_nl=False)

    def ctrl(self, char):
        """Send a CTRL- character to U-Boot.

        This is useful in order to stop execution of long-running synchronous
        commands such as "ums".

        Args:
            char (str): Character to send, e.g. 'C' to send Ctrl-C
        """
        self.log.action(f'Sending Ctrl-{char}')
        self.send(chr(ord(char) - ord('@')))

    def ctrlc(self):
        """Send a CTRL-C character to U-Boot.

        This is useful in order to stop execution of long-running synchronous
        commands such as "ums".
        """
        self.ctrl('C')

    def wait_for(self, text):
        """Wait for a pattern to be emitted by U-Boot.

        This is useful when a long-running command such as "dfu" is executing,
        and it periodically emits some text that should show up at a specific
        location in the log file.

        Args:
            text (str or re): The text to wait for; either a string (containing
                raw text, not a regular expression) or an re object.
        """
        if isinstance(text, str):
            text = re.escape(text)
        m = self.expect([text] + self.bad_patterns)
        if m != 0:
            raise Unexpected(
                "Unexpected pattern found on console (exp '{text}': " +
                self.bad_pattern_ids[m - 1])

    def drain_console(self):
        """Read from and log the U-Boot console for a short time.

        U-Boot's console output is only logged when the test code actively
        waits for U-Boot to emit specific data. There are cases where tests
        can fail without doing this. For example, if a test asks U-Boot to
        enable USB device mode, then polls until a host-side device node
        exists. In such a case, it is useful to log U-Boot's console output
        in case U-Boot printed clues as to why the host-side even did not
        occur. This function will do that.
        """
        # If we are already not connected to U-Boot, there's nothing to drain.
        # This should only happen when a previous call to run_command() or
        # wait_for() failed (and hence the output has already been logged), or
        # the system is shutting down.
        if not self.p:
            return

        orig_timeout = self.timeout
        try:
            # Drain the log for a relatively short time.
            self.timeout = 1000
            # Wait for something U-Boot will likely never send. This will
            # cause the console output to be read and logged.
            self.expect(['This should never match U-Boot output'])
        except:
            # We expect a timeout, since U-Boot won't print what we waited
            # for. Squash it when it happens.
            #
            # Squash any other exception too. This function is only used to
            # drain (and log) the U-Boot console output after a failed test.
            # The U-Boot process will be restarted, or target board reset, once
            # this function returns. So, we don't care about detecting any
            # additional errors, so they're squashed so that the rest of the
            # post-test-failure cleanup code can continue operation, and
            # correctly terminate any log sections, etc.
            pass
        finally:
            self.timeout = orig_timeout

    def ensure_spawned(self, expect_reset=False):
        """Ensure a connection to a correctly running U-Boot instance.

        This may require spawning a new Sandbox process or resetting target
        hardware, as defined by the implementation sub-class.

        This is an internal function and should not be called directly.

        Args:
            expect_reset (bool): Indicates whether this boot is expected
                to be reset while the 1st boot process after main boot before
                prompt. False by default.
        """
        if self.p:
            # Reset the console timeout value as some tests may change
            # its default value during the execution
            self.timeout = self.get_default_timeout()
            return
        try:
            self.log.start_section('Starting U-Boot')
            self.at_prompt = False
            self.timeout = None
            self.p = self.get_spawn()
            # Real targets can take a long time to scroll large amounts of
            # text if LCD is enabled. This value may need tweaking in the
            # future, possibly per-test to be optimal. This works for 'help'
            # on board 'seaboard'.
            self.timeout = self.get_default_timeout()
            self.logfile_read = self.logstream
            if self.config.use_running_system:
                # Send an empty command to set up the 'expect' logic. This has
                # the side effect of ensuring that there was no partial command
                # line entered
                self.run_command(' ')
            else:
                if expect_reset:
                    loop_num = 2
                else:
                    loop_num = 1
                self._wait_for_boot_prompt(loop_num = loop_num)
            self.at_prompt = True
            self.at_prompt_logevt = self.logstream.logfile.cur_evt
        except Exception as ex:
            self.log.error(str(ex))
            self.cleanup_spawn()
            raise
        finally:
            self.log.timestamp()
            self.log.end_section('Starting U-Boot')

    def cleanup_spawn(self):
        """Shut down all interaction with the U-Boot instance.

        This is used when an error is detected prior to re-establishing a
        connection with a fresh U-Boot instance.

        This is an internal function and should not be called directly.
        """
        try:
            if self.p:
                self.p.close()
        except:
            pass
        self.p = None

    def shutdown_required(self):
        """Called to shut down the running U-Boot

        Some tests make changes to U-Boot which cannot be undone within the
        test, such as booting an operating system. This function shuts down
        U-Boot so that a new one will be started for any future tests
        """
        self.drain_console()
        self.cleanup_spawn()

    def restart_uboot(self, expect_reset=False):
        """Shut down and restart U-Boot."""
        self.cleanup_spawn()
        self.ensure_spawned(expect_reset)

    def get_spawn_output(self):
        """Return the start-up output from U-Boot

        Returns:
            The output produced by ensure_spawed(), as a string.
        """
        if self.p:
            return self.get_expect_output()
        return None

    def validate_version_string_in_text(self, text):
        """Assert that a command's output includes the U-Boot signon message.

        This is primarily useful for validating the "version" command without
        duplicating the signon text regex in a test function.

        Args:
            text (str): The command output text to check.

        Raises:
            Assertion if the validation fails.
        """
        assert self.u_boot_version_string in text

    def disable_check(self, check_type):
        """Temporarily disable an error check of U-Boot's output.

        Create a new context manager (for use with the "with" statement) which
        temporarily disables a particular console output error check.

        Args:
            check_type (str): The type of error-check to disable, see
                bad_pattern_defs

        Returns:
            A context manager object.
        """
        return ConsoleDisableCheck(self, check_type)

    def enable_check(self, check_type, check_pattern):
        """Temporarily enable an error check of U-Boot's output.

        Create a new context manager (for use with the "with" statement) which
        temporarily enables a particular console output error check. The
        arguments form a new element of bad_pattern_defs defined above.

        Args:
            check_type (str): The type of error-check to disable, see
                bad_pattern_defs
            check_pattern (re.Pattern): Regex for text error / bad pattern
                to be checked.

        Returns:
            A context manager object.
        """
        return ConsoleEnableCheck(self, check_type, check_pattern)

    def temporary_timeout(self, timeout):
        """Temporarily set up different timeout for commands.

        Create a new context manager (for use with the "with" statement) which
        temporarily change timeout.

        Args:
            timeout (int): Time in milliseconds.

        Returns:
            A context manager object.
        """
        return ConsoleSetupTimeout(self, timeout)

    def expect(self, patterns):
        """Wait for the sub-process to emit specific data.

        This function waits for the process to emit one pattern from the
        supplied list of patterns, or for a timeout to occur.

        Args:
            patterns (list of str or regex.Regex): Patterns we expect to
                see in the sub-process' stdout.

        Returns:
            int: index within the patterns array of the pattern the process
            emitted.

        Notable exceptions:
            Timeout, if the process did not emit any of the patterns within
            the expected time.
        """
        for pi, pat in enumerate(patterns):
            if isinstance(pat, str):
                patterns[pi] = re.compile(pat)

        tstart_s = time.time()
        try:
            while True:
                earliest_m = None
                earliest_pi = None
                for pi, pat in enumerate(patterns):
                    m = pat.search(self.buf)
                    if not m:
                        continue
                    if earliest_m and m.start() >= earliest_m.start():
                        continue
                    earliest_m = m
                    earliest_pi = pi
                if earliest_m:
                    pos = earliest_m.start()
                    posafter = earliest_m.end()
                    self.before = self.buf[:pos]
                    self.after = self.buf[pos:posafter]
                    self.output += self.buf[:posafter]
                    self.buf = self.buf[posafter:]
                    return earliest_pi
                tnow_s = time.time()
                if self.timeout:
                    tdelta_ms = (tnow_s - tstart_s) * 1000
                    poll_maxwait = self.timeout - tdelta_ms
                    if tdelta_ms > self.timeout:
                        raise Timeout()
                else:
                    poll_maxwait = None
                events = self.p.poll.poll(poll_maxwait)
                if not events:
                    raise Timeout()
                # Small delay to let more data accumulate in PTY buffer, to
                # reduce CPU usage for test.py from 100%
                time.sleep(0.001)
                c = self.p.receive(RECV_BUF_SIZE)
                if self.logfile_read:
                    self.logfile_read.write(c)
                # Filter VT100 escapes from new data only, not entire buffer.
                # This avoids O(n^2) behaviour when receiving large output.
                # count=0 is supposed to be the default, which indicates
                # unlimited substitutions, but in practice the version of
                # Python in Ubuntu 14.04 appears to default to count=2!
                c = self.re_vt100.sub('', c, count=1000000)
                self.buf += c
        finally:
            if self.logfile_read:
                self.logfile_read.flush()

    def get_expect_output(self):
        """Return the output read by expect()

        Returns:
            The output processed by expect(), as a string.
        """
        return self.output
