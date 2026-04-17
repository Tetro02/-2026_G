# 2026-04-17T13:08:01.212094
import vitis

client = vitis.create_client()
client.set_workspace(path="7020_BRAM_ADC")

comp = client.create_app_component(name="BRAM_ADC",platform = "$COMPONENT_LOCATION/../platform/export/platform/platform.xpfm",domain = "standalone_ps7_cortexa9_0",template = "hello_world")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="BRAM_ADC")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

