# 2026-04-16T22:54:49.537774600
import vitis

client = vitis.create_client()
client.set_workspace(path="7020_BRAM_ADC")

platform = client.get_component(name="platform")
status = platform.build()

comp = client.get_component(name="hello_world")
comp.build()

status = platform.build()

comp.build()

vitis.dispose()

