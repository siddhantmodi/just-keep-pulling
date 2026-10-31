# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(ads1220.c)
zephyr_library_sources_ifdef(CONFIG_JKP_ADS1220_EMUL ads1220_emul.c)
