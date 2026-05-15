-- Notepatra palette preview — synthetic; no real data

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity counter4 is
    generic (
        WIDTH : positive := 4
    );
    port (
        clk    : in  std_logic;
        rst_n  : in  std_logic;
        enable : in  std_logic;
        count  : out std_logic_vector(WIDTH-1 downto 0);
        carry  : out std_logic
    );
end entity counter4;

architecture rtl of counter4 is
    signal cnt_reg : unsigned(WIDTH-1 downto 0) := (others => '0');
begin

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            cnt_reg <= (others => '0');
        elsif rising_edge(clk) then
            if enable = '1' then
                cnt_reg <= cnt_reg + 1;
            end if;
        end if;
    end process;

    count <= std_logic_vector(cnt_reg);
    carry <= '1' when cnt_reg = (cnt_reg'range => '1') else '0';

end architecture rtl;


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity top is
end entity top;

architecture sim of top is
    signal clk, rst_n, enable, carry : std_logic := '0';
    signal count : std_logic_vector(3 downto 0);
begin

    dut : entity work.counter4
        generic map ( WIDTH => 4 )
        port map (
            clk    => clk,
            rst_n  => rst_n,
            enable => enable,
            count  => count,
            carry  => carry
        );

    clk <= not clk after 5 ns;

    stim : process
    begin
        rst_n  <= '0';
        enable <= '0';
        wait for 12 ns;
        rst_n  <= '1';
        enable <= '1';
        wait for 200 ns;
        wait;
    end process;

end architecture sim;
