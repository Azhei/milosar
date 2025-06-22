- Convert a `.bit` to a `.bin` to be compatible with `fpgautil`:
```
source /opt/Xilinx/Vivado/2022.2/settings64.sh
bootgen -image system_wrapper.bif -arch zynq -process_bitstream bin -w
```