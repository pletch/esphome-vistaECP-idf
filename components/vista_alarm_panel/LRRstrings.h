#pragma once

static constexpr const char* const lrr_msg_values[] = {
    /*   0 */ "ZMedical",
    /*   1 */ "ZPersonal Emergency",
    /*   2 */ "ZFail to report in",
    /*   3 */ "ZFire",
    /*   4 */ "ZSmoke",
    /*   5 */ "ZCombustion",
    /*   6 */ "ZWater Flow",
    /*   7 */ "ZHeat",
    /*   8 */ "ZPull Station",
    /*   9 */ "ZDuct",
    /*  10 */ "ZFlame",
    /*  11 */ "ZNear Alarm",
    /*  12 */ "ZPanic",
    /*  13 */ "UDuress",
    /*  14 */ "ZSilent",
    /*  15 */ "ZAudible",
    /*  16 */ "ZDuress - Access granted",
    /*  17 */ "ZDuress - Egress granted",
    /*  18 */ "UHoldup suspicion print",
    /*  19 */ "URemote Silent Panic",
    /*  20 */ "ZPanic Verifier",
    /*  21 */ "ZBurglary",
    /*  22 */ "ZPerimeter",
    /*  23 */ "ZInterior",
    /*  24 */ "Z24 Hour (Safe)",
    /*  25 */ "ZEntry/Exit",
    /*  26 */ "ZDay/Night",
    /*  27 */ "ZOutdoor",
    /*  28 */ "ZTamper",
    /*  29 */ "ZNear alarm",
    /*  30 */ "ZIntrusion Verifier",
    /*  31 */ "ZGeneral Alarm",
    /*  32 */ "ZPolling loop open",
    /*  33 */ "ZPolling loop short",
    /*  34 */ "ZExpansion module failure",
    /*  35 */ "ZSensor tamper",
    /*  36 */ "ZExpansion module tamper",
    /*  37 */ "ZSilent Burglary",
    /*  38 */ "ZSensor Supervision Failure",
    /*  39 */ "Z24 Hour NonBurglary",
    /*  40 */ "ZGas detected",
    /*  41 */ "ZRefrigeration",
    /*  42 */ "ZLoss of heat",
    /*  43 */ "ZWater Leakage",
    /*  44 */ "ZFoil Break",
    /*  45 */ "ZDay Trouble",
    /*  46 */ "ZLow bottled gas level",
    /*  47 */ "ZHigh temp",
    /*  48 */ "ZLow temp",
    /*  49 */ "ZAwareness Zone Response",
    /*  50 */ "ZLoss of air flow",
    /*  51 */ "ZCarbon Monoxide detected",
    /*  52 */ "ZTank level",
    /*  53 */ "ZHigh Humidity",
    /*  54 */ "ZLow Humidity",
    /*  55 */ "ZFire Supervisory",
    /*  56 */ "ZLow water pressure",
    /*  57 */ "ZLow CO2",
    /*  58 */ "ZGate valve sensor",
    /*  59 */ "ZLow water level",
    /*  60 */ "ZPump activated",
    /*  61 */ "ZPump failure",
    /*  62 */ "ZSystem Trouble",
    /*  63 */ "ZAC Loss",
    /*  64 */ "ZLow system battery",
    /*  65 */ "ZRAM Checksum bad",
    /*  66 */ "ZROM checksum bad",
    /*  67 */ "ZSystem reset",
    /*  68 */ "ZPanel programming changed",
    /*  69 */ "ZSelftest failure",
    /*  70 */ "ZSystem shutdown",
    /*  71 */ "ZBattery test failure",
    /*  72 */ "ZGround fault",
    /*  73 */ "ZBattery Missing/Dead",
    /*  74 */ "ZPower Supply Overcurrent",
    /*  75 */ "UEngineer Reset",
    /*  76 */ "ZPrimary Power Supply Failure",
    /*  77 */ "ZSystem Tamper",
    /*  78 */ "ZControl Panel System Tamper",
    /*  79 */ "ZSounder/Relay",
    /*  80 */ "ZBell 1",
    /*  81 */ "ZBell 2",
    /*  82 */ "ZAlarm relay",
    /*  83 */ "ZTrouble relay",
    /*  84 */ "ZReversing relay",
    /*  85 */ "ZNotification Appliance Ckt. #3",
    /*  86 */ "ZNotification Appliance Ckt. #4",
    /*  87 */ "ZSystem Peripheral trouble",
    /*  88 */ "ZExpansion module failure",  // 330 - same label, different subsystem
    /*  89 */ "ZPolling loop open",           // 331
    /*  90 */ "ZPolling loop short",          // 332
    /*  91 */ "ZRepeater failure",
    /*  92 */ "ZLocal printer out of paper",
    /*  93 */ "ZLocal printer failure",
    /*  94 */ "ZExp. Module DC Loss",
    /*  95 */ "ZExp. Module Low Batt.",
    /*  96 */ "ZExp. Module Reset",
    /*  97 */ "ZExp. Module Tamper",
    /*  98 */ "ZExp. Module AC Loss",
    /*  99 */ "ZExp. Module selftest fail",
    /* 100 */ "ZRF Receiver Jam Detect",
    /* 101 */ "ZAES Encryption disabled/enabled",
    /* 102 */ "ZCommunication trouble",
    /* 103 */ "ZTelco 1 fault",
    /* 104 */ "ZTelco 2 fault",
    /* 105 */ "ZLong Range Radio xmitter fault",
    /* 106 */ "ZFailure to communicate event",
    /* 107 */ "ZLoss of Radio supervision",
    /* 108 */ "ZLoss of central polling",
    /* 109 */ "ZLong Range Radio VSWR problem",
    /* 110 */ "ZPeriodic Comm Test Fail/Restore",
    /* 111 */ "ZNew Registration",
    /* 112 */ "ZAuthorized Substitution Registration",
    /* 113 */ "ZUnauthorized Substitution Registration",
    /* 114 */ "ZModule Firmware Update Start/Finish",
    /* 115 */ "ZModule Firmware Update Failed",
    /* 116 */ "ZProtection loop",
    /* 117 */ "ZProtection loop open",
    /* 118 */ "ZProtection loop short",
    /* 119 */ "ZFire trouble",
    /* 120 */ "ZExit error alarm (zone)",
    /* 121 */ "ZPanic zone trouble",
    /* 122 */ "ZHoldup zone trouble",
    /* 123 */ "ZSwinger Trouble",
    /* 124 */ "ZCrosszone Trouble",
    /* 125 */ "ZSensor trouble",
    /* 126 */ "ZLoss of supervision RF",
    /* 127 */ "ZLoss of supervision RPM",
    /* 128 */ "ZSensor tamper",
    /* 129 */ "ZRF low battery",
    /* 130 */ "ZSmoke detector Hi sensitivity",
    /* 131 */ "ZSmoke detector Low sensitivity",
    /* 132 */ "ZIntrusion detector Hi sensitivity",
    /* 133 */ "ZIntrusion detector Low sensitivity",
    /* 134 */ "ZSensor selftest failure",
    /* 135 */ "ZSensor Watch trouble",
    /* 136 */ "ZDrift Compensation Error",
    /* 137 */ "ZMaintenance Alert",
    /* 138 */ "ZCO Detector needs replacement",
    /* 139 */ "UOpen/Close",
    /* 140 */ "UArmed AWAY",
    /* 141 */ "UGroup O/C",
    /* 142 */ "UAutomatic O/C",
    /* 143 */ "ULate to O/C",
    /* 144 */ "UDeferred O/C",
    /* 145 */ "UCancel",
    /* 146 */ "URemote arm/disarm",
    /* 147 */ "UQuick arm",
    /* 148 */ "UKeyswitch O/C",
    /* 149 */ "UCallback request made",
    /* 150 */ "USuccessful download/access",
    /* 151 */ "UUnsuccessful access",
    /* 152 */ "USystem shutdown command received",
    /* 153 */ "UDialer shutdown command received",
    /* 154 */ "ZSuccessful Upload",
    /* 155 */ "URemote Cancel",
    /* 156 */ "URemote Verify",
    /* 157 */ "UAccess denied",
    /* 158 */ "UAccess report by user",
    /* 159 */ "ZForced Access",
    /* 160 */ "UEgress Denied",
    /* 161 */ "UEgress Granted",
    /* 162 */ "ZAccess Door propped open",
    /* 163 */ "ZAccess point Door Status Monitor trouble",
    /* 164 */ "ZAccess point Request To Exit trouble",
    /* 165 */ "UAccess program mode entry",
    /* 166 */ "UAccess program mode exit",
    /* 167 */ "UAccess threat level change",
    /* 168 */ "ZAccess relay/trigger fail",
    /* 169 */ "ZAccess RTE shunt",
    /* 170 */ "ZAccess DSM shunt",
    /* 171 */ "USecond Person Access",
    /* 172 */ "UIrregular Access",
    /* 173 */ "UArmed STAY",
    /* 174 */ "UKeyswitch Armed STAY",
    /* 175 */ "UArmed with System Trouble Override",
    /* 176 */ "UException O/C",
    /* 177 */ "UEarly O/C",
    /* 178 */ "ULate O/C",
    /* 179 */ "UFailed to Open",
    /* 180 */ "UFailed to Close",
    /* 181 */ "UAutoarm Failed",
    /* 182 */ "UPartial Arm",
    /* 183 */ "UExit Error (user)",
    /* 184 */ "UUser on Premises",
    /* 185 */ "URecent Close",
    /* 186 */ "ZWrong Code Entry",
    /* 187 */ "ULegal Code Entry",
    /* 188 */ "URearm after Alarm",
    /* 189 */ "UAutoarm Time Extended",
    /* 190 */ "ZPanic Alarm Reset",
    /* 191 */ "UService On/Off Premises",
    /* 192 */ "ZAccess reader disable",
    /* 193 */ "ZSounder/Relay Disable",
    /* 194 */ "ZBell 1 disable",
    /* 195 */ "ZBell 2 disable",
    /* 196 */ "ZAlarm relay disable",
    /* 197 */ "ZTrouble relay disable",
    /* 198 */ "ZReversing relay disable",
    /* 199 */ "ZNotification Appliance Ckt. #3 disable",
    /* 200 */ "ZNotification Appliance Ckt. #4 disable",
    /* 201 */ "ZModule Added",
    /* 202 */ "ZModule Removed",
    /* 203 */ "ZDialer disabled",
    /* 204 */ "ZRadio transmitter disabled",
    /* 205 */ "ZRemote Upload/Download disabled",
    /* 206 */ "ZZone/Sensor bypass",
    /* 207 */ "ZFire bypass",
    /* 208 */ "Z24 Hour zone bypass",
    /* 209 */ "ZBurg. Bypass",
    /* 210 */ "UGroup bypass",
    /* 211 */ "ZSwinger bypass",
    /* 212 */ "ZAccess zone shunt",
    /* 213 */ "ZAccess point bypass",
    /* 214 */ "ZVault Bypass",
    /* 215 */ "ZVent Zone Bypass",
    /* 216 */ "ZManual trigger test report",
    /* 217 */ "ZPeriodic test report",
    /* 218 */ "ZPeriodic RF transmission",
    /* 219 */ "UFire test",
    /* 220 */ "ZStatus report to follow",
    /* 221 */ "ZListenin to follow",
    /* 222 */ "UWalk test mode",
    /* 223 */ "ZPeriodic test - System Trouble Present",
    /* 224 */ "ZVideo Xmitter active",
    /* 225 */ "ZPoint tested OK",
    /* 226 */ "ZPoint not tested",
    /* 227 */ "ZIntrusion Zone Walk Tested",
    /* 228 */ "ZFire Zone Walk Tested",
    /* 229 */ "ZPanic Zone Walk Tested",
    /* 230 */ "ZService Request",
    /* 231 */ "ZEvent Log reset",
    /* 232 */ "ZEvent Log 50% full",
    /* 233 */ "ZEvent Log 90% full",
    /* 234 */ "ZEvent Log overflow",
    /* 235 */ "UTime/Date reset",
    /* 236 */ "ZTime/Date inaccurate",
    /* 237 */ "ZProgram mode entry",
    /* 238 */ "ZProgram mode exit",
    /* 239 */ "Z32 Hour Event log marker",
    /* 240 */ "ZSchedule change",
    /* 241 */ "ZException schedule change",
    /* 242 */ "ZAccess schedule change",
    /* 243 */ "ZSenior Watch Trouble",
    /* 244 */ "ULatchkey Supervision",
    /* 245 */ "ZRestricted Door Opened",
    /* 246 */ "ZHelp Arrived",
    /* 247 */ "ZAdditional Help Needed",
    /* 248 */ "ZAdditional Help Cancel",
    /* 249 */ "ZReserved for Ademco Use",
    /* 250 */ "UReserved for Ademco Use",
    /* 251 */ "ZSystem Inactivity",
    /* 252 */ "UUser Code modified by Installer",
    /* 253 */ "ZAuxiliary #3",
    /* 254 */ "ZInstaller Test",
    /* 255 */ "ZUser Assigned",  // shared by codes 922-961
    /* 256 */ "ZUnable to output signal (Derived Channel)",
    /* 257 */ "ZSTU Controller down (Derived Channel)",
    /* 258 */ "ZDownload Abort",
    /* 259 */ "ZDownload Start/End",
    /* 260 */ "ZDownload Interrupted",
    /* 261 */ "ZDevice Flash Update Started/Completed",
    /* 262 */ "ZDevice Flash Update Failed",
    /* 263 */ "ZAutoclose with Bypass",
    /* 264 */ "ZBypass Closing",
    /* 265 */ "ZFire Alarm Silence",
    /* 266 */ "USupervisory Point test Start/End",
    /* 267 */ "UHoldup test Start/End",
    /* 268 */ "UBurg. Test Print Start/End",
    /* 269 */ "USupervisory Test Print Start/End",
    /* 270 */ "ZBurg. Diagnostics Start/End",
    /* 271 */ "ZFire Diagnostics Start/End",
    /* 272 */ "ZUntyped diagnostics",
    /* 273 */ "UTrouble Closing",
    /* 274 */ "UAccess Denied Code Unknown",
    /* 275 */ "ZSupervisory Point Alarm",
    /* 276 */ "ZSupervisory Point Bypass",
    /* 277 */ "ZSupervisory Point Trouble",
    /* 278 */ "ZHoldup Point Bypass",
    /* 279 */ "ZAC Failure for 4 hours",
    /* 280 */ "ZOutput Trouble",
    /* 281 */ "UUser code for event",
    /* 282 */ "ULogoff",
    /* 283 */ "ZCS Connection Failure",
    /* 284 */ "ZRcvr Database Connection Fail/Restore",
    /* 285 */ "ZLicense Expiration Notify",
    /* 286 */ "Z1 and 1/3 Day No Read Log",
    /* 287 */ "ZUnknown",  // default / fallback — always last
};

static constexpr int LRR_MSG_UNKNOWN_IDX = 287;

struct LrrEntry {
    uint16_t code;
    uint16_t idx;
};

// Must remain sorted by code for binary search.
// Verified by static_assert below.
static constexpr LrrEntry lrr_lookup_table[] = {
    {100,   0},  // Medical
    {101,   1},  // Personal Emergency
    {102,   2},  // Fail to report in
    {110,   3},  // Fire
    {111,   4},  // Smoke
    {112,   5},  // Combustion
    {113,   6},  // Water Flow
    {114,   7},  // Heat
    {115,   8},  // Pull Station
    {116,   9},  // Duct
    {117,  10},  // Flame
    {118,  11},  // Near Alarm
    {120,  12},  // Panic
    {121,  13},  // Duress
    {122,  14},  // Silent
    {123,  15},  // Audible
    {124,  16},  // Duress - Access granted
    {125,  17},  // Duress - Egress granted
    {126,  18},  // Holdup suspicion print
    {127,  19},  // Remote Silent Panic
    {129,  20},  // Panic Verifier
    {130,  21},  // Burglary
    {131,  22},  // Perimeter
    {132,  23},  // Interior
    {133,  24},  // 24 Hour (Safe)
    {134,  25},  // Entry/Exit
    {135,  26},  // Day/Night
    {136,  27},  // Outdoor
    {137,  28},  // Tamper
    {138,  29},  // Near alarm
    {139,  30},  // Intrusion Verifier
    {140,  31},  // General Alarm
    {141,  32},  // Polling loop open
    {142,  33},  // Polling loop short
    {143,  34},  // Expansion module failure
    {144,  35},  // Sensor tamper
    {145,  36},  // Expansion module tamper
    {146,  37},  // Silent Burglary
    {147,  38},  // Sensor Supervision Failure
    {150,  39},  // 24 Hour NonBurglary
    {151,  40},  // Gas detected
    {152,  41},  // Refrigeration
    {153,  42},  // Loss of heat
    {154,  43},  // Water Leakage
    {155,  44},  // Foil Break
    {156,  45},  // Day Trouble
    {157,  46},  // Low bottled gas level
    {158,  47},  // High temp
    {159,  48},  // Low temp
    {160,  49},  // Awareness Zone Response
    {161,  50},  // Loss of air flow
    {162,  51},  // Carbon Monoxide detected
    {163,  52},  // Tank level
    {168,  53},  // High Humidity
    {169,  54},  // Low Humidity
    {200,  55},  // Fire Supervisory
    {201,  56},  // Low water pressure
    {202,  57},  // Low CO2
    {203,  58},  // Gate valve sensor
    {204,  59},  // Low water level
    {205,  60},  // Pump activated
    {206,  61},  // Pump failure
    {300,  62},  // System Trouble
    {301,  63},  // AC Loss
    {302,  64},  // Low system battery
    {303,  65},  // RAM Checksum bad
    {304,  66},  // ROM checksum bad
    {305,  67},  // System reset
    {306,  68},  // Panel programming changed
    {307,  69},  // Selftest failure
    {308,  70},  // System shutdown
    {309,  71},  // Battery test failure
    {310,  72},  // Ground fault
    {311,  73},  // Battery Missing/Dead
    {312,  74},  // Power Supply Overcurrent
    {313,  75},  // Engineer Reset
    {314,  76},  // Primary Power Supply Failure
    {315,  62},  // System Trouble (same label as 300)
    {316,  77},  // System Tamper
    {317,  78},  // Control Panel System Tamper
    {320,  79},  // Sounder/Relay
    {321,  80},  // Bell 1
    {322,  81},  // Bell 2
    {323,  82},  // Alarm relay
    {324,  83},  // Trouble relay
    {325,  84},  // Reversing relay
    {326,  85},  // Notification Appliance Ckt. #3
    {327,  86},  // Notification Appliance Ckt. #4
    {330,  87},  // System Peripheral trouble
    {331,  88},  // Expansion module failure (bus)
    {332,  89},  // Polling loop open (bus)
    {333,  90},  // Polling loop short (bus)
    {334,  91},  // Repeater failure
    {335,  92},  // Local printer out of paper
    {336,  93},  // Local printer failure
    {337,  94},  // Exp. Module DC Loss
    {338,  95},  // Exp. Module Low Batt.
    {339,  96},  // Exp. Module Reset
    {341,  97},  // Exp. Module Tamper
    {342,  98},  // Exp. Module AC Loss
    {343,  99},  // Exp. Module selftest fail
    {344, 100},  // RF Receiver Jam Detect
    {345, 101},  // AES Encryption disabled/enabled
    {350, 102},  // Communication trouble
    {351, 103},  // Telco 1 fault
    {352, 104},  // Telco 2 fault
    {353, 105},  // Long Range Radio xmitter fault
    {354, 106},  // Failure to communicate event
    {355, 107},  // Loss of Radio supervision
    {356, 108},  // Loss of central polling
    {357, 109},  // Long Range Radio VSWR problem
    {358, 110},  // Periodic Comm Test Fail/Restore
    {360, 111},  // New Registration
    {361, 112},  // Authorized Substitution Registration
    {362, 113},  // Unauthorized Substitution Registration
    {365, 114},  // Module Firmware Update Start/Finish
    {366, 115},  // Module Firmware Update Failed
    {370, 116},  // Protection loop
    {371, 117},  // Protection loop open
    {372, 118},  // Protection loop short
    {373, 119},  // Fire trouble
    {374, 120},  // Exit error alarm (zone)
    {375, 121},  // Panic zone trouble
    {376, 122},  // Holdup zone trouble
    {377, 123},  // Swinger Trouble
    {378, 124},  // Crosszone Trouble
    {380, 125},  // Sensor trouble
    {381, 126},  // Loss of supervision RF
    {382, 127},  // Loss of supervision RPM
    {383, 128},  // Sensor tamper
    {384, 129},  // RF low battery
    {385, 130},  // Smoke detector Hi sensitivity
    {386, 131},  // Smoke detector Low sensitivity
    {387, 132},  // Intrusion detector Hi sensitivity
    {388, 133},  // Intrusion detector Low sensitivity
    {389, 134},  // Sensor selftest failure
    {391, 135},  // Sensor Watch trouble
    {392, 136},  // Drift Compensation Error
    {393, 137},  // Maintenance Alert
    {394, 138},  // CO Detector needs replacement
    {400, 139},  // Open/Close
    {401, 140},  // Armed AWAY
    {402, 141},  // Group O/C
    {403, 142},  // Automatic O/C
    {404, 143},  // Late to O/C
    {405, 144},  // Deferred O/C
    {406, 145},  // Cancel
    {407, 146},  // Remote arm/disarm
    {408, 147},  // Quick arm
    {409, 148},  // Keyswitch O/C
    {411, 149},  // Callback request made
    {412, 150},  // Successful download/access
    {413, 151},  // Unsuccessful access
    {414, 152},  // System shutdown command received
    {415, 153},  // Dialer shutdown command received
    {416, 154},  // Successful Upload
    {418, 155},  // Remote Cancel
    {419, 156},  // Remote Verify
    {421, 157},  // Access denied
    {422, 158},  // Access report by user
    {423, 159},  // Forced Access
    {424, 160},  // Egress Denied
    {425, 161},  // Egress Granted
    {426, 162},  // Access Door propped open
    {427, 163},  // Access point DSM trouble
    {428, 164},  // Access point RTE trouble
    {429, 165},  // Access program mode entry
    {430, 166},  // Access program mode exit
    {431, 167},  // Access threat level change
    {432, 168},  // Access relay/trigger fail
    {433, 169},  // Access RTE shunt
    {434, 170},  // Access DSM shunt
    {435, 171},  // Second Person Access
    {436, 172},  // Irregular Access
    {441, 173},  // Armed STAY
    {442, 174},  // Keyswitch Armed STAY
    {443, 175},  // Armed with System Trouble Override
    {450, 176},  // Exception O/C
    {451, 177},  // Early O/C
    {452, 178},  // Late O/C
    {453, 179},  // Failed to Open
    {454, 180},  // Failed to Close
    {455, 181},  // Autoarm Failed
    {456, 182},  // Partial Arm
    {457, 183},  // Exit Error (user)
    {458, 184},  // User on Premises
    {459, 185},  // Recent Close
    {461, 186},  // Wrong Code Entry
    {462, 187},  // Legal Code Entry
    {463, 188},  // Rearm after Alarm
    {464, 189},  // Autoarm Time Extended
    {465, 190},  // Panic Alarm Reset
    {466, 191},  // Service On/Off Premises
    {501, 192},  // Access reader disable
    {520, 193},  // Sounder/Relay Disable
    {521, 194},  // Bell 1 disable
    {522, 195},  // Bell 2 disable
    {523, 196},  // Alarm relay disable
    {524, 197},  // Trouble relay disable
    {525, 198},  // Reversing relay disable
    {526, 199},  // Notification Appliance Ckt. #3 disable
    {527, 200},  // Notification Appliance Ckt. #4 disable
    {531, 201},  // Module Added
    {532, 202},  // Module Removed
    {551, 203},  // Dialer disabled
    {552, 204},  // Radio transmitter disabled
    {553, 205},  // Remote Upload/Download disabled
    {570, 206},  // Zone/Sensor bypass
    {571, 207},  // Fire bypass
    {572, 208},  // 24 Hour zone bypass
    {573, 209},  // Burg. Bypass
    {574, 210},  // Group bypass
    {575, 211},  // Swinger bypass
    {576, 212},  // Access zone shunt
    {577, 213},  // Access point bypass
    {578, 214},  // Vault Bypass
    {579, 215},  // Vent Zone Bypass
    {601, 216},  // Manual trigger test report
    {602, 217},  // Periodic test report
    {603, 218},  // Periodic RF transmission
    {604, 219},  // Fire test
    {605, 220},  // Status report to follow
    {606, 221},  // Listenin to follow
    {607, 222},  // Walk test mode
    {608, 223},  // Periodic test - System Trouble Present
    {609, 224},  // Video Xmitter active
    {611, 225},  // Point tested OK
    {612, 226},  // Point not tested
    {613, 227},  // Intrusion Zone Walk Tested
    {614, 228},  // Fire Zone Walk Tested
    {615, 229},  // Panic Zone Walk Tested
    {616, 230},  // Service Request
    {621, 231},  // Event Log reset
    {622, 232},  // Event Log 50% full
    {623, 233},  // Event Log 90% full
    {624, 234},  // Event Log overflow
    {625, 235},  // Time/Date reset
    {626, 236},  // Time/Date inaccurate
    {627, 237},  // Program mode entry
    {628, 238},  // Program mode exit
    {629, 239},  // 32 Hour Event log marker
    {630, 240},  // Schedule change
    {631, 241},  // Exception schedule change
    {632, 242},  // Access schedule change
    {641, 243},  // Senior Watch Trouble
    {642, 244},  // Latchkey Supervision
    {643, 245},  // Restricted Door Opened
    {645, 246},  // Help Arrived
    {646, 247},  // Additional Help Needed
    {647, 248},  // Additional Help Cancel
    {651, 249},  // Reserved for Ademco Use (Z)
    {652, 250},  // Reserved for Ademco Use (U)
    {653, 250},  // Reserved for Ademco Use (U)
    {654, 251},  // System Inactivity
    {655, 252},  // User Code modified by Installer
    {703, 253},  // Auxiliary #3
    {704, 254},  // Installer Test
    {750, 255},  // User Assigned (750-789, 920-929 share this)
    {751, 255}, {752, 255}, {753, 255}, {754, 255}, {755, 255},
    {756, 255}, {757, 255}, {758, 255}, {759, 255}, {760, 255},
    {761, 255}, {762, 255}, {763, 255}, {764, 255}, {765, 255},
    {766, 255}, {767, 255}, {768, 255}, {769, 255}, {770, 255},
    {771, 255}, {772, 255}, {773, 255}, {774, 255}, {775, 255},
    {776, 255}, {777, 255}, {778, 255}, {779, 255}, {780, 255},
    {781, 255}, {782, 255}, {783, 255}, {784, 255}, {785, 255},
    {786, 255}, {787, 255}, {788, 255}, {789, 255},
    {796, 256},  // Unable to output signal (Derived Channel)
    {798, 257},  // STU Controller down (Derived Channel)
    {900, 258},  // Download Abort
    {901, 259},  // Download Start/End
    {902, 260},  // Download Interrupted
    {903, 261},  // Device Flash Update Started/Completed
    {904, 262},  // Device Flash Update Failed
    {910, 263},  // Autoclose with Bypass
    {911, 264},  // Bypass Closing
    {912, 265},  // Fire Alarm Silence
    {913, 266},  // Supervisory Point test Start/End
    {914, 267},  // Holdup test Start/End
    {915, 268},  // Burg. Test Print Start/End
    {916, 269},  // Supervisory Test Print Start/End
    {917, 270},  // Burg. Diagnostics Start/End
    {918, 271},  // Fire Diagnostics Start/End
    {919, 272},  // Untyped diagnostics
    {920, 273},  // Trouble Closing
    {921, 274},  // Access Denied Code Unknown
    {922, 275},  // Supervisory Point Alarm
    {923, 276},  // Supervisory Point Bypass
    {924, 277},  // Supervisory Point Trouble
    {925, 278},  // Holdup Point Bypass
    {926, 279},  // AC Failure for 4 hours
    {927, 280},  // Output Trouble
    {928, 281},  // User code for event
    {929, 282},  // Logoff
    {954, 283},  // CS Connection Failure
    {961, 284},  // Rcvr Database Connection Fail/Restore
    {962, 285},  // License Expiration Notify
    {999, 286},  // 1 and 1/3 Day No Read Log
};

static constexpr int LRR_TABLE_SIZE = sizeof(lrr_lookup_table) / sizeof(lrr_lookup_table[0]);

inline const char* lrr_msg_lookup(int statusCode)
{
    // Binary search over lrr_lookup_table (sorted by code).
    int lo = 0, hi = LRR_TABLE_SIZE - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (lrr_lookup_table[mid].code == statusCode)
            return lrr_msg_values[lrr_lookup_table[mid].idx];
        else if (lrr_lookup_table[mid].code < statusCode)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return lrr_msg_values[LRR_MSG_UNKNOWN_IDX];
}

