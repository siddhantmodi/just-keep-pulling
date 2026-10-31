# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_JKP_ADS1220 ads1220)
add_subdirectory_ifdef(CONFIG_JKP_NPM1300_EMUL npm1300_emul)
