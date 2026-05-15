// Notepatra palette preview — synthetic; no real data
// Simple 4-bit counter with synchronous reset.

`timescale 1ns / 1ps

module counter4 #(
    parameter WIDTH = 4
) (
    input  wire             clk,
    input  wire             rst_n,
    input  wire             enable,
    output reg  [WIDTH-1:0] count,
    output wire             carry
);

    assign carry = &count;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count <= {WIDTH{1'b0}};
        else if (enable)
            count <= count + 1'b1;
        else
            count <= count;
    end

endmodule


module tb;

    reg              clk;
    reg              rst_n;
    reg              enable;
    wire [3:0]       count;
    wire             carry;

    counter4 #(.WIDTH(4)) dut (
        .clk    (clk),
        .rst_n  (rst_n),
        .enable (enable),
        .count  (count),
        .carry  (carry)
    );

    // Clock
    initial clk = 1'b0;
    always #5 clk = ~clk;

    initial begin
        $display("Notepatra Verilog demo start");
        rst_n  = 1'b0;
        enable = 1'b0;
        #12;
        rst_n  = 1'b1;
        enable = 1'b1;

        #200;

        $display("final count = %0d, carry = %b", count, carry);
        $finish;
    end

    initial begin
        $monitor("t=%0t  count=%0d  carry=%b", $time, count, carry);
    end

endmodule
