obj-$(CONFIG_$(PHASE_)PHY_CADENCE_TORRENT) += phy-cadence-torrent.o

# There is no SPL_PHY_CADENCE_SIERRA, so build this only in U-Boot proper
ifndef CONFIG_XPL_BUILD
obj-$(CONFIG_PHY_CADENCE_SIERRA)	+= phy-cadence-sierra.o
endif
