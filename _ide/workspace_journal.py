# 2026-04-15T08:25:21.882480900
import vitis

client = vitis.create_client()
client.set_workspace(path="7020_BRAM_ADC")

vitis.dispose()

