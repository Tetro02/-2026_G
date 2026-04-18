# 2026-04-18T13:03:12.758941800
import vitis

client = vitis.create_client()
client.set_workspace(path="7020_BRAM_ADC")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

comp = client.get_component(name="BRAM_ADC")
comp.set_app_config(key = "USER_LINK_LIBRARIES", values = [""])

comp.set_app_config(key = "USER_LINK_LIBRARIES", values = [""])

comp.set_app_config(key = "USER_LINK_LIBRARIES", values = ["CMSISDSP"])

comp.set_app_config(key = "USER_LINK_DIRECTORIES", values = ["C:/Users/Tetro/Documents/FPGA_Project/MyExample/7020_BRAM_ADC/BRAM_ADC/src"])

status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

comp = client.get_component(name="BRAM_ADC")
comp.set_app_config(key = "USER_INCLUDE_DIRECTORIES", values = [""])

comp.set_app_config(key = "USER_INCLUDE_DIRECTORIES", values = ["C:/Users/Tetro/Documents/FPGA_Project/MyExample/7020_BRAM_ADC/BRAM_ADC/src/CMSIS_DSP"])

comp.set_app_config(key = "USER_INCLUDE_DIRECTORIES", values = ["CMSIS_DSP", "C:/Users/Tetro/Documents/FPGA_Project/MyExample/7020_BRAM_ADC/BRAM_ADC/src/CMSIS_DSP/dsp"])

status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

status = platform.build()

comp.build()

comp = client.get_component(name="BRAM_ADC")
comp.set_app_config(key = "USER_COMPILE_DEFINITIONS", values = ["__ARM_FEATURE_SIMD_MACRO_INTRINSICS=0"])

comp.set_app_config(key = "USER_COMPILE_DEFINITIONS", values = [""])

status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

