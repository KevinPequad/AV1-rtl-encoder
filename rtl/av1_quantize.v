// av1_quantize.v — AV1 Forward Quantization
// Uses qindex-based dequant lookup (8-bit only, from AV1 spec)
// Forward quant: qcoeff = round(coeff / dequant)
// Reference: SVT-AV1/Source/Lib/Codec/inv_transforms.c dc_qlookup_QTX / ac_qlookup_QTX

module av1_quantize (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire        is_dc,        // 1 = DC coefficient, 0 = AC
    input  wire [7:0]  qindex,       // quantization index 0-255
    output reg         done,

    // Input coefficient (16-bit signed)
    input  wire signed [15:0] coeff_in,

    // Output quantized coefficient (16-bit signed)
    output reg  signed [15:0] qcoeff_out,
    // Output dequant value for inverse quant
    output reg  [15:0] dequant_out
);

    // DC dequant lookup (8-bit), from SVT-AV1 dc_qlookup_QTX[0..255]
    // Stored in a ROM; only first 256 entries for 8-bit
    reg [15:0] dc_dequant_rom [0:255];
    reg [15:0] ac_dequant_rom [0:255];

    initial begin
        // DC dequant table (from dc_qlookup_QTX, 8-bit)
        dc_dequant_rom[  0]=4;   dc_dequant_rom[  1]=8;   dc_dequant_rom[  2]=8;   dc_dequant_rom[  3]=9;
        dc_dequant_rom[  4]=10;  dc_dequant_rom[  5]=11;  dc_dequant_rom[  6]=12;  dc_dequant_rom[  7]=12;
        dc_dequant_rom[  8]=13;  dc_dequant_rom[  9]=14;  dc_dequant_rom[ 10]=15;  dc_dequant_rom[ 11]=16;
        dc_dequant_rom[ 12]=17;  dc_dequant_rom[ 13]=18;  dc_dequant_rom[ 14]=19;  dc_dequant_rom[ 15]=19;
        dc_dequant_rom[ 16]=20;  dc_dequant_rom[ 17]=21;  dc_dequant_rom[ 18]=22;  dc_dequant_rom[ 19]=23;
        dc_dequant_rom[ 20]=24;  dc_dequant_rom[ 21]=25;  dc_dequant_rom[ 22]=26;  dc_dequant_rom[ 23]=26;
        dc_dequant_rom[ 24]=27;  dc_dequant_rom[ 25]=28;  dc_dequant_rom[ 26]=29;  dc_dequant_rom[ 27]=30;
        dc_dequant_rom[ 28]=31;  dc_dequant_rom[ 29]=32;  dc_dequant_rom[ 30]=32;  dc_dequant_rom[ 31]=33;
        dc_dequant_rom[ 32]=34;  dc_dequant_rom[ 33]=35;  dc_dequant_rom[ 34]=36;  dc_dequant_rom[ 35]=37;
        dc_dequant_rom[ 36]=38;  dc_dequant_rom[ 37]=38;  dc_dequant_rom[ 38]=39;  dc_dequant_rom[ 39]=40;
        dc_dequant_rom[ 40]=41;  dc_dequant_rom[ 41]=42;  dc_dequant_rom[ 42]=43;  dc_dequant_rom[ 43]=43;
        dc_dequant_rom[ 44]=44;  dc_dequant_rom[ 45]=45;  dc_dequant_rom[ 46]=46;  dc_dequant_rom[ 47]=47;
        dc_dequant_rom[ 48]=48;  dc_dequant_rom[ 49]=48;  dc_dequant_rom[ 50]=49;  dc_dequant_rom[ 51]=50;
        dc_dequant_rom[ 52]=51;  dc_dequant_rom[ 53]=52;  dc_dequant_rom[ 54]=53;  dc_dequant_rom[ 55]=53;
        dc_dequant_rom[ 56]=54;  dc_dequant_rom[ 57]=55;  dc_dequant_rom[ 58]=56;  dc_dequant_rom[ 59]=57;
        dc_dequant_rom[ 60]=57;  dc_dequant_rom[ 61]=58;  dc_dequant_rom[ 62]=59;  dc_dequant_rom[ 63]=60;
        dc_dequant_rom[ 64]=61;  dc_dequant_rom[ 65]=62;  dc_dequant_rom[ 66]=62;  dc_dequant_rom[ 67]=63;
        dc_dequant_rom[ 68]=64;  dc_dequant_rom[ 69]=65;  dc_dequant_rom[ 70]=66;  dc_dequant_rom[ 71]=66;
        dc_dequant_rom[ 72]=67;  dc_dequant_rom[ 73]=68;  dc_dequant_rom[ 74]=69;  dc_dequant_rom[ 75]=70;
        dc_dequant_rom[ 76]=70;  dc_dequant_rom[ 77]=71;  dc_dequant_rom[ 78]=72;  dc_dequant_rom[ 79]=73;
        dc_dequant_rom[ 80]=74;  dc_dequant_rom[ 81]=74;  dc_dequant_rom[ 82]=75;  dc_dequant_rom[ 83]=76;
        dc_dequant_rom[ 84]=77;  dc_dequant_rom[ 85]=78;  dc_dequant_rom[ 86]=78;  dc_dequant_rom[ 87]=79;
        dc_dequant_rom[ 88]=80;  dc_dequant_rom[ 89]=81;  dc_dequant_rom[ 90]=81;  dc_dequant_rom[ 91]=82;
        dc_dequant_rom[ 92]=83;  dc_dequant_rom[ 93]=84;  dc_dequant_rom[ 94]=85;  dc_dequant_rom[ 95]=85;
        dc_dequant_rom[ 96]=87;  dc_dequant_rom[ 97]=88;  dc_dequant_rom[ 98]=90;  dc_dequant_rom[ 99]=92;
        dc_dequant_rom[100]=93;  dc_dequant_rom[101]=95;  dc_dequant_rom[102]=96;  dc_dequant_rom[103]=98;
        dc_dequant_rom[104]=99;  dc_dequant_rom[105]=101; dc_dequant_rom[106]=102; dc_dequant_rom[107]=104;
        dc_dequant_rom[108]=105; dc_dequant_rom[109]=107; dc_dequant_rom[110]=108; dc_dequant_rom[111]=110;
        dc_dequant_rom[112]=111; dc_dequant_rom[113]=113; dc_dequant_rom[114]=114; dc_dequant_rom[115]=116;
        dc_dequant_rom[116]=117; dc_dequant_rom[117]=118; dc_dequant_rom[118]=120; dc_dequant_rom[119]=121;
        dc_dequant_rom[120]=123; dc_dequant_rom[121]=125; dc_dequant_rom[122]=127; dc_dequant_rom[123]=129;
        dc_dequant_rom[124]=131; dc_dequant_rom[125]=134; dc_dequant_rom[126]=136; dc_dequant_rom[127]=138;
        dc_dequant_rom[128]=140; dc_dequant_rom[129]=142; dc_dequant_rom[130]=144; dc_dequant_rom[131]=146;
        dc_dequant_rom[132]=148; dc_dequant_rom[133]=150; dc_dequant_rom[134]=152; dc_dequant_rom[135]=154;
        dc_dequant_rom[136]=156; dc_dequant_rom[137]=158; dc_dequant_rom[138]=161; dc_dequant_rom[139]=164;
        dc_dequant_rom[140]=166; dc_dequant_rom[141]=169; dc_dequant_rom[142]=172; dc_dequant_rom[143]=174;
        dc_dequant_rom[144]=177; dc_dequant_rom[145]=180; dc_dequant_rom[146]=182; dc_dequant_rom[147]=185;
        dc_dequant_rom[148]=187; dc_dequant_rom[149]=190; dc_dequant_rom[150]=192; dc_dequant_rom[151]=195;
        dc_dequant_rom[152]=199; dc_dequant_rom[153]=202; dc_dequant_rom[154]=205; dc_dequant_rom[155]=208;
        dc_dequant_rom[156]=211; dc_dequant_rom[157]=214; dc_dequant_rom[158]=217; dc_dequant_rom[159]=220;
        dc_dequant_rom[160]=223; dc_dequant_rom[161]=226; dc_dequant_rom[162]=230; dc_dequant_rom[163]=233;
        dc_dequant_rom[164]=237; dc_dequant_rom[165]=240; dc_dequant_rom[166]=243; dc_dequant_rom[167]=247;
        dc_dequant_rom[168]=250; dc_dequant_rom[169]=253; dc_dequant_rom[170]=257; dc_dequant_rom[171]=261;
        dc_dequant_rom[172]=265; dc_dequant_rom[173]=269; dc_dequant_rom[174]=272; dc_dequant_rom[175]=276;
        dc_dequant_rom[176]=280; dc_dequant_rom[177]=284; dc_dequant_rom[178]=288; dc_dequant_rom[179]=292;
        dc_dequant_rom[180]=296; dc_dequant_rom[181]=300; dc_dequant_rom[182]=304; dc_dequant_rom[183]=309;
        dc_dequant_rom[184]=313; dc_dequant_rom[185]=317; dc_dequant_rom[186]=322; dc_dequant_rom[187]=326;
        dc_dequant_rom[188]=330; dc_dequant_rom[189]=335; dc_dequant_rom[190]=340; dc_dequant_rom[191]=344;
        dc_dequant_rom[192]=349; dc_dequant_rom[193]=354; dc_dequant_rom[194]=359; dc_dequant_rom[195]=364;
        dc_dequant_rom[196]=369; dc_dequant_rom[197]=374; dc_dequant_rom[198]=379; dc_dequant_rom[199]=384;
        dc_dequant_rom[200]=389; dc_dequant_rom[201]=395; dc_dequant_rom[202]=400; dc_dequant_rom[203]=406;
        dc_dequant_rom[204]=411; dc_dequant_rom[205]=417; dc_dequant_rom[206]=423; dc_dequant_rom[207]=429;
        dc_dequant_rom[208]=435; dc_dequant_rom[209]=441; dc_dequant_rom[210]=447; dc_dequant_rom[211]=454;
        dc_dequant_rom[212]=461; dc_dequant_rom[213]=467; dc_dequant_rom[214]=475; dc_dequant_rom[215]=482;
        dc_dequant_rom[216]=489; dc_dequant_rom[217]=497; dc_dequant_rom[218]=505; dc_dequant_rom[219]=513;
        dc_dequant_rom[220]=522; dc_dequant_rom[221]=530; dc_dequant_rom[222]=539; dc_dequant_rom[223]=549;
        dc_dequant_rom[224]=559; dc_dequant_rom[225]=569; dc_dequant_rom[226]=579; dc_dequant_rom[227]=590;
        dc_dequant_rom[228]=602; dc_dequant_rom[229]=614; dc_dequant_rom[230]=626; dc_dequant_rom[231]=640;
        dc_dequant_rom[232]=654; dc_dequant_rom[233]=668; dc_dequant_rom[234]=684; dc_dequant_rom[235]=700;
        dc_dequant_rom[236]=717; dc_dequant_rom[237]=736; dc_dequant_rom[238]=755; dc_dequant_rom[239]=775;
        dc_dequant_rom[240]=796; dc_dequant_rom[241]=819; dc_dequant_rom[242]=843; dc_dequant_rom[243]=869;
        dc_dequant_rom[244]=896; dc_dequant_rom[245]=925; dc_dequant_rom[246]=955; dc_dequant_rom[247]=988;
        dc_dequant_rom[248]=1022;dc_dequant_rom[249]=1058;dc_dequant_rom[250]=1098;dc_dequant_rom[251]=1139;
        dc_dequant_rom[252]=1184;dc_dequant_rom[253]=1232;dc_dequant_rom[254]=1282;dc_dequant_rom[255]=1336;

        // AC dequant table (from ac_qlookup_QTX, 8-bit)
        ac_dequant_rom[  0]=4;   ac_dequant_rom[  1]=8;   ac_dequant_rom[  2]=9;   ac_dequant_rom[  3]=10;
        ac_dequant_rom[  4]=11;  ac_dequant_rom[  5]=12;  ac_dequant_rom[  6]=13;  ac_dequant_rom[  7]=14;
        ac_dequant_rom[  8]=15;  ac_dequant_rom[  9]=16;  ac_dequant_rom[ 10]=17;  ac_dequant_rom[ 11]=18;
        ac_dequant_rom[ 12]=19;  ac_dequant_rom[ 13]=20;  ac_dequant_rom[ 14]=21;  ac_dequant_rom[ 15]=22;
        ac_dequant_rom[ 16]=23;  ac_dequant_rom[ 17]=24;  ac_dequant_rom[ 18]=25;  ac_dequant_rom[ 19]=26;
        ac_dequant_rom[ 20]=27;  ac_dequant_rom[ 21]=28;  ac_dequant_rom[ 22]=29;  ac_dequant_rom[ 23]=30;
        ac_dequant_rom[ 24]=31;  ac_dequant_rom[ 25]=32;  ac_dequant_rom[ 26]=33;  ac_dequant_rom[ 27]=34;
        ac_dequant_rom[ 28]=35;  ac_dequant_rom[ 29]=36;  ac_dequant_rom[ 30]=37;  ac_dequant_rom[ 31]=38;
        ac_dequant_rom[ 32]=39;  ac_dequant_rom[ 33]=40;  ac_dequant_rom[ 34]=41;  ac_dequant_rom[ 35]=42;
        ac_dequant_rom[ 36]=43;  ac_dequant_rom[ 37]=44;  ac_dequant_rom[ 38]=45;  ac_dequant_rom[ 39]=46;
        ac_dequant_rom[ 40]=47;  ac_dequant_rom[ 41]=48;  ac_dequant_rom[ 42]=49;  ac_dequant_rom[ 43]=50;
        ac_dequant_rom[ 44]=51;  ac_dequant_rom[ 45]=52;  ac_dequant_rom[ 46]=53;  ac_dequant_rom[ 47]=54;
        ac_dequant_rom[ 48]=55;  ac_dequant_rom[ 49]=56;  ac_dequant_rom[ 50]=57;  ac_dequant_rom[ 51]=58;
        ac_dequant_rom[ 52]=59;  ac_dequant_rom[ 53]=60;  ac_dequant_rom[ 54]=61;  ac_dequant_rom[ 55]=62;
        ac_dequant_rom[ 56]=63;  ac_dequant_rom[ 57]=64;  ac_dequant_rom[ 58]=65;  ac_dequant_rom[ 59]=66;
        ac_dequant_rom[ 60]=67;  ac_dequant_rom[ 61]=68;  ac_dequant_rom[ 62]=69;  ac_dequant_rom[ 63]=70;
        ac_dequant_rom[ 64]=71;  ac_dequant_rom[ 65]=72;  ac_dequant_rom[ 66]=73;  ac_dequant_rom[ 67]=74;
        ac_dequant_rom[ 68]=75;  ac_dequant_rom[ 69]=76;  ac_dequant_rom[ 70]=77;  ac_dequant_rom[ 71]=78;
        ac_dequant_rom[ 72]=79;  ac_dequant_rom[ 73]=80;  ac_dequant_rom[ 74]=81;  ac_dequant_rom[ 75]=82;
        ac_dequant_rom[ 76]=83;  ac_dequant_rom[ 77]=84;  ac_dequant_rom[ 78]=85;  ac_dequant_rom[ 79]=86;
        ac_dequant_rom[ 80]=87;  ac_dequant_rom[ 81]=88;  ac_dequant_rom[ 82]=89;  ac_dequant_rom[ 83]=90;
        ac_dequant_rom[ 84]=91;  ac_dequant_rom[ 85]=92;  ac_dequant_rom[ 86]=93;  ac_dequant_rom[ 87]=94;
        ac_dequant_rom[ 88]=95;  ac_dequant_rom[ 89]=96;  ac_dequant_rom[ 90]=97;  ac_dequant_rom[ 91]=98;
        ac_dequant_rom[ 92]=99;  ac_dequant_rom[ 93]=100; ac_dequant_rom[ 94]=101; ac_dequant_rom[ 95]=102;
        ac_dequant_rom[ 96]=104; ac_dequant_rom[ 97]=106; ac_dequant_rom[ 98]=108; ac_dequant_rom[ 99]=110;
        ac_dequant_rom[100]=112; ac_dequant_rom[101]=114; ac_dequant_rom[102]=116; ac_dequant_rom[103]=118;
        ac_dequant_rom[104]=120; ac_dequant_rom[105]=122; ac_dequant_rom[106]=124; ac_dequant_rom[107]=126;
        ac_dequant_rom[108]=128; ac_dequant_rom[109]=130; ac_dequant_rom[110]=132; ac_dequant_rom[111]=134;
        ac_dequant_rom[112]=136; ac_dequant_rom[113]=138; ac_dequant_rom[114]=140; ac_dequant_rom[115]=142;
        ac_dequant_rom[116]=144; ac_dequant_rom[117]=146; ac_dequant_rom[118]=148; ac_dequant_rom[119]=150;
        ac_dequant_rom[120]=152; ac_dequant_rom[121]=155; ac_dequant_rom[122]=158; ac_dequant_rom[123]=161;
        ac_dequant_rom[124]=164; ac_dequant_rom[125]=167; ac_dequant_rom[126]=170; ac_dequant_rom[127]=173;
        ac_dequant_rom[128]=176; ac_dequant_rom[129]=179; ac_dequant_rom[130]=182; ac_dequant_rom[131]=185;
        ac_dequant_rom[132]=188; ac_dequant_rom[133]=191; ac_dequant_rom[134]=194; ac_dequant_rom[135]=197;
        ac_dequant_rom[136]=200; ac_dequant_rom[137]=203; ac_dequant_rom[138]=207; ac_dequant_rom[139]=211;
        ac_dequant_rom[140]=215; ac_dequant_rom[141]=219; ac_dequant_rom[142]=223; ac_dequant_rom[143]=227;
        ac_dequant_rom[144]=231; ac_dequant_rom[145]=235; ac_dequant_rom[146]=239; ac_dequant_rom[147]=243;
        ac_dequant_rom[148]=247; ac_dequant_rom[149]=251; ac_dequant_rom[150]=255; ac_dequant_rom[151]=260;
        ac_dequant_rom[152]=265; ac_dequant_rom[153]=270; ac_dequant_rom[154]=275; ac_dequant_rom[155]=280;
        ac_dequant_rom[156]=285; ac_dequant_rom[157]=290; ac_dequant_rom[158]=295; ac_dequant_rom[159]=300;
        ac_dequant_rom[160]=305; ac_dequant_rom[161]=311; ac_dequant_rom[162]=317; ac_dequant_rom[163]=323;
        ac_dequant_rom[164]=329; ac_dequant_rom[165]=335; ac_dequant_rom[166]=341; ac_dequant_rom[167]=347;
        ac_dequant_rom[168]=353; ac_dequant_rom[169]=359; ac_dequant_rom[170]=366; ac_dequant_rom[171]=373;
        ac_dequant_rom[172]=380; ac_dequant_rom[173]=387; ac_dequant_rom[174]=394; ac_dequant_rom[175]=401;
        ac_dequant_rom[176]=408; ac_dequant_rom[177]=416; ac_dequant_rom[178]=424; ac_dequant_rom[179]=432;
        ac_dequant_rom[180]=440; ac_dequant_rom[181]=448; ac_dequant_rom[182]=456; ac_dequant_rom[183]=465;
        ac_dequant_rom[184]=474; ac_dequant_rom[185]=483; ac_dequant_rom[186]=492; ac_dequant_rom[187]=501;
        ac_dequant_rom[188]=510; ac_dequant_rom[189]=520; ac_dequant_rom[190]=530; ac_dequant_rom[191]=540;
        ac_dequant_rom[192]=550; ac_dequant_rom[193]=560; ac_dequant_rom[194]=571; ac_dequant_rom[195]=582;
        ac_dequant_rom[196]=593; ac_dequant_rom[197]=604; ac_dequant_rom[198]=615; ac_dequant_rom[199]=627;
        ac_dequant_rom[200]=639; ac_dequant_rom[201]=651; ac_dequant_rom[202]=663; ac_dequant_rom[203]=676;
        ac_dequant_rom[204]=689; ac_dequant_rom[205]=702; ac_dequant_rom[206]=715; ac_dequant_rom[207]=729;
        ac_dequant_rom[208]=743; ac_dequant_rom[209]=757; ac_dequant_rom[210]=771; ac_dequant_rom[211]=786;
        ac_dequant_rom[212]=801; ac_dequant_rom[213]=816; ac_dequant_rom[214]=832; ac_dequant_rom[215]=848;
        ac_dequant_rom[216]=864; ac_dequant_rom[217]=881; ac_dequant_rom[218]=898; ac_dequant_rom[219]=915;
        ac_dequant_rom[220]=933; ac_dequant_rom[221]=951; ac_dequant_rom[222]=969; ac_dequant_rom[223]=988;
        ac_dequant_rom[224]=1007;ac_dequant_rom[225]=1026;ac_dequant_rom[226]=1046;ac_dequant_rom[227]=1066;
        ac_dequant_rom[228]=1087;ac_dequant_rom[229]=1108;ac_dequant_rom[230]=1129;ac_dequant_rom[231]=1151;
        ac_dequant_rom[232]=1173;ac_dequant_rom[233]=1196;ac_dequant_rom[234]=1219;ac_dequant_rom[235]=1243;
        ac_dequant_rom[236]=1267;ac_dequant_rom[237]=1292;ac_dequant_rom[238]=1317;ac_dequant_rom[239]=1343;
        ac_dequant_rom[240]=1369;ac_dequant_rom[241]=1396;ac_dequant_rom[242]=1423;ac_dequant_rom[243]=1451;
        ac_dequant_rom[244]=1479;ac_dequant_rom[245]=1508;ac_dequant_rom[246]=1537;ac_dequant_rom[247]=1567;
        ac_dequant_rom[248]=1597;ac_dequant_rom[249]=1628;ac_dequant_rom[250]=1660;ac_dequant_rom[251]=1692;
        ac_dequant_rom[252]=1725;ac_dequant_rom[253]=1759;ac_dequant_rom[254]=1793;ac_dequant_rom[255]=1828;
    end

    reg [15:0] dequant;
    reg signed [15:0] abs_coeff;
    reg        sign;
    reg [1:0]  stage;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done       <= 0;
            qcoeff_out <= 0;
            dequant_out<= 0;
            stage      <= 0;
        end else begin
            done <= 0;

            if (start) begin
                // Look up dequant value
                dequant   <= is_dc ? dc_dequant_rom[qindex] : ac_dequant_rom[qindex];
                abs_coeff <= (coeff_in[15]) ? -coeff_in : coeff_in;
                sign      <= coeff_in[15];
                stage     <= 1;
            end else if (stage == 2'd1) begin
                // Quantize: qcoeff = (abs_coeff + dequant/2) / dequant
                if (dequant == 0) begin
                    qcoeff_out <= 0;
                end else begin
                    if (sign)
                        qcoeff_out <= -$signed({1'b0, (abs_coeff + (dequant >> 1)) / dequant});
                    else
                        qcoeff_out <= $signed({1'b0, (abs_coeff + (dequant >> 1)) / dequant});
                end
                dequant_out <= dequant;
                done        <= 1;
                stage       <= 0;
            end
        end
    end

endmodule
