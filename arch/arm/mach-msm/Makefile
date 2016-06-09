obj-y += proc_comm.o
obj-y += io.o irq.o timer.o dma.o memory.o
obj-y += vreg.o
obj-y += pmic.o
obj-$(CONFIG_ARCH_MSM_ARM11) += acpuclock-arm11.o idle.o
obj-$(CONFIG_ARCH_MSM_SCORPION) += idle-v7.o
obj-$(CONFIG_ARCH_MSM_SCORPION) += arch-init-scorpion.o
obj-$(CONFIG_ARCH_QSD8X50) += acpuclock-qsd8x50.o
obj-$(CONFIG_ARCH_MSM7X30) += acpuclock-7x30.o
obj-y += clock.o clock-pcom.o
obj-y += gpio.o generic_gpio.o
obj-y += nand_partitions.o

obj-$(CONFIG_ARCH_QSD8X50) += sirc.o
obj-$(CONFIG_MSM_FIQ_SUPPORT) += fiq_glue.o
obj-$(CONFIG_MSM_SMD) += smd.o smd_debug.o
obj-$(CONFIG_MSM_SMD) += smd_tty.o smd_qmi.o
obj-$(CONFIG_MSM_SMD) += last_radio_log.o
obj-$(CONFIG_MSM_DAL) += dal.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter_device.o
obj-$(CONFIG_MSM_ONCRPCROUTER) += smd_rpcrouter_servers.o
obj-$(CONFIG_MSM_RPCSERVERS) += rpc_server_dog_keepalive.o
obj-$(CONFIG_MSM_RPCSERVERS) += rpc_server_time_remote.o
obj-$(CONFIG_MSM_ADSP) += qdsp5/
obj-$(CONFIG_MSM_QDSP5V2) += qdsp5v2/
obj-$(CONFIG_MSM_QDSP6) += qdsp6/
obj-$(CONFIG_MSM_HW3D) += hw3d.o
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_MSM_REMOTE_SPINLOCK) += remote_spinlock.o
obj-$(CONFIG_MSM_SSBI) += ssbi.o

obj-$(CONFIG_ARCH_QSD8X50) += devices-qsd8x50.o

obj-$(CONFIG_MACH_BRAVO) += board-bravo.o board-bravo-panel.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-keypad.o board-bravo-mmc.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-rfkill.o htc_wifi_nvs.o htc_awb_cal.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-wifi.o board-bravo-audio.o
obj-$(CONFIG_MACH_BRAVO) += msm_vibrator.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-microp.o
obj-$(CONFIG_MACH_BRAVO) += htc_acoustic_qsd.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-tpa2018d1.o
obj-$(CONFIG_MACH_BRAVO) += board-bravo-smb329.o
obj-$(CONFIG_MACH_BRAVO) += devices_htc.o
obj-$(CONFIG_MACH_BRAVO) += bravo/

obj-$(CONFIG_HTC_PWRSINK) += htc_pwrsink.o
obj-$(CONFIG_HTC_PWRSPLY) += htc_power_supply.o
obj-$(CONFIG_HTC_HEADSET) += htc_headset.o
obj-$(CONFIG_HTC_35MM_JACK) += htc_35mm_jack.o


obj-$(CONFIG_ARCH_MSM_FLASHLIGHT) += msm_flashlight.o

