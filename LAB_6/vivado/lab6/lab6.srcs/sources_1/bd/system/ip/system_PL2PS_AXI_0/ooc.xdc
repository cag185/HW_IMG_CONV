# aclk {FREQ_HZ 100000000 CLK_DOMAIN system_PS7_0_FCLK_CLK0 PHASE 0.000}
# Clock Domain: system_PS7_0_FCLK_CLK0
create_clock -name aclk -period 10.000 [get_ports aclk]
# Generated clocks
