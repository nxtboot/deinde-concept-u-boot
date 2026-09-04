# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>
#

"""Common Claude Agent SDK utilities.

Provides shared functions for running Claude agents across tools that need
AI assistance (e.g. pickman, patman review).
"""

from u_boot_pylib import tout

# Markers in an agent failure which mean every later call will fail too: the
# tool is too old, or not signed in, or asked for a model it cannot serve.
# Retrying these achieves nothing, so a caller which loops needs to know
FATAL_MARKERS = (
    'does not support this model',
    'claude_code_version_too_old',
    'is required. Run \'claude update\'',
    'authentication_error',
    'invalid_api_key',
    'Please run /login',
)


# Set once a failure is seen which will repeat, so that a caller which loops
# can stop instead of retrying the same thing for ever
fatal_seen = None  # pylint: disable=invalid-name


def is_fatal_error(text):
    """Check whether an agent failure will repeat on every future call

    Args:
        text (str): The error text

    Return:
        bool: True if the failure is in the setup rather than the request
    """
    return any(mark in text for mark in FATAL_MARKERS)

# Maximum buffer size for agent responses
MAX_BUFFER_SIZE = 10 * 1024 * 1024  # 10MB

# Check if claude_agent_sdk is available
try:
    from claude_agent_sdk import query, ClaudeAgentOptions
    AGENT_AVAILABLE = True
except ImportError:
    AGENT_AVAILABLE = False


def check_available():
    """Check if the Claude Agent SDK is available

    Returns:
        bool: True if available, False otherwise
    """
    if not AGENT_AVAILABLE:
        tout.error('Claude Agent SDK not available')
        tout.error('Install with: pip install claude-agent-sdk')
        return False
    return True


async def run_agent_collect(prompt, options):
    """Run a Claude agent and collect its conversation log

    Sends the prompt to a Claude agent, streams output to stdout and
    collects all text blocks into a conversation log.

    Args:
        prompt (str): The prompt to send to the agent
        options (ClaudeAgentOptions): Agent configuration

    Returns:
        tuple: (success, conversation_log) where success is bool and
            conversation_log is the agent's output text
    """
    import os
    debug = os.environ.get('PATMAN_DEBUG_AGENT')
    conversation_log = []
    try:
        async for message in query(prompt=prompt, options=options):
            if debug:
                tout.error(f'AGENT MSG: {type(message).__name__}: '
                           f'{message!r}')
            if hasattr(message, 'content'):
                for block in message.content:
                    if debug:
                        tout.error(f'  BLOCK: {type(block).__name__}: '
                                   f'{block!r}')
                    if hasattr(block, 'text'):
                        print(block.text)
                        conversation_log.append(block.text)
        return True, '\n\n'.join(conversation_log)
    except (RuntimeError, ValueError, OSError) as exc:
        _report_failure(exc)
        return False, '\n\n'.join(conversation_log)
    except Exception as exc:
        if 'API Error' in str(exc) or 'exit code' in str(exc):
            _report_failure(exc)
            return False, '\n\n'.join(conversation_log)
        raise


def _report_failure(exc):
    """Report an agent failure, saying if it will repeat

    Args:
        exc (Exception): The failure
    """
    global fatal_seen  # pylint: disable=global-statement
    tout.error(f'Agent failed: {exc}')
    if is_fatal_error(str(exc)):
        fatal_seen = str(exc)
        tout.error('This is a problem with the setup rather than with this '
                   'request, so every later agent call will fail the same '
                   'way until it is put right')
