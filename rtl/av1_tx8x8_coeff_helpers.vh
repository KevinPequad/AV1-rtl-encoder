// TX_8X8 coefficient helper functions for the reduced RTL-owned raw path.
// These tables are packed in the icdf_flat word order expected by av1_entropy.v.

function [5:0] scan_8x8_pos;
    input [5:0] idx;
    begin
        case (idx)
            6'd0: scan_8x8_pos = 6'd0;
            6'd1: scan_8x8_pos = 6'd8;
            6'd2: scan_8x8_pos = 6'd1;
            6'd3: scan_8x8_pos = 6'd2;
            6'd4: scan_8x8_pos = 6'd9;
            6'd5: scan_8x8_pos = 6'd16;
            6'd6: scan_8x8_pos = 6'd24;
            6'd7: scan_8x8_pos = 6'd17;
            6'd8: scan_8x8_pos = 6'd10;
            6'd9: scan_8x8_pos = 6'd3;
            6'd10: scan_8x8_pos = 6'd4;
            6'd11: scan_8x8_pos = 6'd11;
            6'd12: scan_8x8_pos = 6'd18;
            6'd13: scan_8x8_pos = 6'd25;
            6'd14: scan_8x8_pos = 6'd32;
            6'd15: scan_8x8_pos = 6'd40;
            6'd16: scan_8x8_pos = 6'd33;
            6'd17: scan_8x8_pos = 6'd26;
            6'd18: scan_8x8_pos = 6'd19;
            6'd19: scan_8x8_pos = 6'd12;
            6'd20: scan_8x8_pos = 6'd5;
            6'd21: scan_8x8_pos = 6'd6;
            6'd22: scan_8x8_pos = 6'd13;
            6'd23: scan_8x8_pos = 6'd20;
            6'd24: scan_8x8_pos = 6'd27;
            6'd25: scan_8x8_pos = 6'd34;
            6'd26: scan_8x8_pos = 6'd41;
            6'd27: scan_8x8_pos = 6'd48;
            6'd28: scan_8x8_pos = 6'd56;
            6'd29: scan_8x8_pos = 6'd49;
            6'd30: scan_8x8_pos = 6'd42;
            6'd31: scan_8x8_pos = 6'd35;
            6'd32: scan_8x8_pos = 6'd28;
            6'd33: scan_8x8_pos = 6'd21;
            6'd34: scan_8x8_pos = 6'd14;
            6'd35: scan_8x8_pos = 6'd7;
            6'd36: scan_8x8_pos = 6'd15;
            6'd37: scan_8x8_pos = 6'd22;
            6'd38: scan_8x8_pos = 6'd29;
            6'd39: scan_8x8_pos = 6'd36;
            6'd40: scan_8x8_pos = 6'd43;
            6'd41: scan_8x8_pos = 6'd50;
            6'd42: scan_8x8_pos = 6'd57;
            6'd43: scan_8x8_pos = 6'd58;
            6'd44: scan_8x8_pos = 6'd51;
            6'd45: scan_8x8_pos = 6'd44;
            6'd46: scan_8x8_pos = 6'd37;
            6'd47: scan_8x8_pos = 6'd30;
            6'd48: scan_8x8_pos = 6'd23;
            6'd49: scan_8x8_pos = 6'd31;
            6'd50: scan_8x8_pos = 6'd38;
            6'd51: scan_8x8_pos = 6'd45;
            6'd52: scan_8x8_pos = 6'd52;
            6'd53: scan_8x8_pos = 6'd59;
            6'd54: scan_8x8_pos = 6'd60;
            6'd55: scan_8x8_pos = 6'd53;
            6'd56: scan_8x8_pos = 6'd46;
            6'd57: scan_8x8_pos = 6'd39;
            6'd58: scan_8x8_pos = 6'd47;
            6'd59: scan_8x8_pos = 6'd54;
            6'd60: scan_8x8_pos = 6'd61;
            6'd61: scan_8x8_pos = 6'd62;
            6'd62: scan_8x8_pos = 6'd55;
            6'd63: scan_8x8_pos = 6'd63;
            default: scan_8x8_pos = 6'd0;
        endcase
    end
endfunction

function [5:0] nz_map_ctx_offset_8x8_fn;
    input [5:0] idx;
    begin
        case (idx)
            6'd0: nz_map_ctx_offset_8x8_fn = 6'd0;
            6'd1: nz_map_ctx_offset_8x8_fn = 6'd1;
            6'd2: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd3: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd4: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd5: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd6: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd7: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd8: nz_map_ctx_offset_8x8_fn = 6'd1;
            6'd9: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd10: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd11: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd12: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd13: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd14: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd15: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd16: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd17: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd18: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd19: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd20: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd21: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd22: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd23: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd24: nz_map_ctx_offset_8x8_fn = 6'd6;
            6'd25: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd26: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd27: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd28: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd29: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd30: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd31: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd32: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd33: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd34: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd35: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd36: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd37: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd38: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd39: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd40: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd41: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd42: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd43: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd44: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd45: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd46: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd47: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd48: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd49: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd50: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd51: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd52: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd53: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd54: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd55: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd56: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd57: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd58: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd59: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd60: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd61: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd62: nz_map_ctx_offset_8x8_fn = 6'd21;
            6'd63: nz_map_ctx_offset_8x8_fn = 6'd21;
            default: nz_map_ctx_offset_8x8_fn = 6'd0;
        endcase
    end
endfunction

function [255:0] coeff_base_ctx_icdf_flat;
    input [5:0] ctx;
    begin
        case (ctx)
            6'd0: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd10626,16'd15820,16'd25014};
            6'd1: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd77,16'd438,16'd7098};
            6'd2: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd774,16'd3543,16'd17105};
            6'd3: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd3610,16'd9480,16'd22890};
            6'd4: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd8432,16'd15680,16'd26349};
            6'd5: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd15729,16'd21765,16'd28909};
            6'd6: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd43,16'd173,16'd5206};
            6'd7: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd369,16'd2180,16'd15193};
            6'd8: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd2459,16'd7930,16'd21949};
            6'd9: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd6852,16'd14082,16'd25644};
            6'd10: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd13428,16'd20080,16'd28289};
            6'd11: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd95,16'd292,16'd4383};
            6'd12: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd830,16'd3763,16'd17462};
            6'd13: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd4446,16'd11153,16'd23831};
            6'd14: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd9982,16'd17165,16'd26786};
            6'd15: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd16632,16'd22501,16'd29148};
            6'd16: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd101,16'd304,16'd5488};
            6'd17: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd764,16'd3608,16'd17161};
            6'd18: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd4028,16'd10633,16'd23677};
            6'd19: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd8748,16'd16136,16'd26536};
            6'd20: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd15096,16'd21391,16'd28721};
            6'd21: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd50,16'd138,16'd3548};
            6'd22: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd306,16'd1548,16'd13118};
            6'd23: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd1941,16'd6456,16'd19718};
            6'd24: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd5300,16'd11898,16'd23540};
            6'd25: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd10797,16'd17619,16'd26622};
            default: coeff_base_ctx_icdf_flat = {192'd0,16'd0,16'd8192,16'd16384,16'd24576};
        endcase
    end
endfunction

    function [255:0] coeff_base_eob_ctx_icdf_flat;
    input [2:0] ctx;
    begin
        case (ctx)
            3'd0: coeff_base_eob_ctx_icdf_flat = {208'd0,16'd0,16'd1725,16'd11311};
            3'd1: coeff_base_eob_ctx_icdf_flat = {208'd0,16'd0,16'd285,16'd817};
            3'd2: coeff_base_eob_ctx_icdf_flat = {208'd0,16'd0,16'd206,16'd615};
            3'd3: coeff_base_eob_ctx_icdf_flat = {208'd0,16'd0,16'd553,16'd1295};
            default: coeff_base_eob_ctx_icdf_flat = {208'd0,16'd0,16'd553,16'd1295};
        endcase
    end
endfunction

    function [255:0] coeff_br_ctx_icdf_flat_fn;
    input [4:0] ctx;
    begin
        case (ctx)
            5'd0: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd4878,16'd7955,16'd14494};
            5'd1: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd5765,16'd9619,16'd17231};
            5'd2: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd10941,16'd16028,16'd23319};
            5'd3: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd15507,16'd20270,16'd26068};
            5'd4: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd18570,16'd22902,16'd27780};
            5'd5: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd20866,16'd24621,16'd28532};
            5'd6: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd24114,16'd26908,16'd29901};
            5'd7: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd6667,16'd9597,16'd15644};
            5'd8: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd2620,16'd5291,16'd12372};
            5'd9: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd4276,16'd8139,16'd16195};
            5'd10: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd7094,16'd11922,16'd20019};
            5'd11: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd9950,16'd14890,16'd22535};
            5'd12: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd12405,16'd17436,16'd24243};
            5'd13: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd16513,16'd21136,16'd26485};
            5'd14: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd3482,16'd6257,16'd12302};
            5'd15: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd1577,16'd3594,16'd9709};
            5'd16: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd2527,16'd5505,16'd13287};
            5'd17: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd4631,16'd9137,16'd17310};
            5'd18: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd7075,16'd12160,16'd20352};
            5'd19: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd9507,16'd14757,16'd22507};
            5'd20: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd13102,16'd18113,16'd24752};
            default: coeff_br_ctx_icdf_flat_fn = {192'd0,16'd0,16'd13102,16'd18113,16'd24752};
        endcase
    end
endfunction

    function [255:0] eob_extra_ctx_icdf_flat_fn;
    input [3:0] ctx;
    begin
        case (ctx)
            4'd0: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd12530};
            4'd1: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd11711};
            4'd2: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd13609};
            4'd3: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd10431};
            4'd4: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd12609};
            4'd5: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd16384};
            4'd6: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd16384};
            4'd7: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd16384};
            4'd8: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd16384};
            default: eob_extra_ctx_icdf_flat_fn = {224'd0,16'd0,16'd16384};
        endcase
    end
endfunction
