# 2026-04-17T17:27:12.394033100
import vitis

client = vitis.create_client()
client.set_workspace(path="7020_BRAM_ADC")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

comp = client.get_component(name="BRAM_ADC")
comp.set_app_config(key = "USER_COMPILE_SOURCES", values = ["main.c", "platform.c"])

status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

comp = client.get_component(name="BRAM_ADC")
comp.set_app_config(key = "USER_COMPILE_SOURCES", values = ["main.c", "platform.c"])

status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

