#pragma once
const char* const msg_values[] = 
{
       "ZMedical",
       "ZPersonal Emergency",
       "ZFail to report in",
       "ZFire",
       "ZSmoke",
       "ZCombustion",
       "ZWater Flow",
       "ZHeat",
       "ZPull Station",
       "ZDuct",
       "ZFlame",
       "ZNear Alarm",
       "ZPanic",
       "UDuress",
       "ZSilent",
       "ZAudible",
       "ZDuress – Access granted",
       "ZDuress – Egress granted",
       "UHoldup suspicion print",
       "URemote Silent Panic",
       "ZPanic Verifier",
       "ZBurglary",
       "ZPerimeter",
       "ZInterior",
       "Z24 Hour (Safe)",
       "ZEntry/Exit",
       "ZDay/Night",
       "ZOutdoor",
       "ZTamper",
       "ZNear alarm",
       "ZIntrusion Verifier",
       "ZGeneral Alarm",
       "ZPolling loop open",
       "ZPolling loop short",
       "ZExpansion module failure",
       "ZSensor tamper",
       "ZExpansion module tamper",
       "ZSilent Burglary",
       "ZSensor Supervision Failure",
       "Z24 Hour NonBurglary",
       "ZGas detected",
       "ZRefrigeration",
       "ZLoss of heat",
       "ZWater Leakage",
       "ZFoil Break",
       "ZDay Trouble",
       "ZLow bottled gas level",
       "ZHigh temp",
       "ZLow temp",
       "ZAwareness Zone Response",
       "ZLoss of air flow",
       "ZCarbon Monoxide detected",
       "ZTank level",
       "ZHigh Humidity",
       "ZLow Humidity",
       "ZFire Supervisory",
       "ZLow water pressure",
       "ZLow CO2",
       "ZGate valve sensor",
       "ZLow water level",
       "ZPump activated",
       "ZPump failure",
       "ZSystem Trouble",
       "ZAC Loss",
       "ZLow system battery",
       "ZRAM Checksum bad",
       "ZROM checksum bad",
       "ZSystem reset",
       "ZPanel programming changed",
       "ZSelftest failure",
       "ZSystem shutdown",
       "ZBattery test failure",
       "ZGround fault",
       "ZBattery Missing/Dead",
       "ZPower Supply Overcurrent",
       "UEngineer Reset",
       "ZPrimary Power Supply Failure",
       "ZSystem Trouble",
       "ZSystem Tamper",
       "ZControl Panel System Tamper",
       "ZSounder/Relay",
       "ZBell 1",
       "ZBell 2",
       "ZAlarm relay",
       "ZTrouble relay",
       "ZReversing relay",
       "ZNotification Appliance Ckt. # 3",
       "ZNotification Appliance Ckt. #4",
       "ZSystem Peripheral trouble",
       "ZPolling loop open",
       "ZPolling loop short",
       "ZExpansion module failure",
       "ZRepeater failure",
       "ZLocal printer out of paper",
       "ZLocal printer failure",
       "ZExp. Module DC Loss",
       "ZExp. Module Low Batt.",
       "ZExp. Module Reset",
       "ZExp. Module Tamper",
       "ZExp. Module AC Loss",
       "ZExp. Module selftest fail",
       "ZRF Receiver Jam Detect",
       "ZAES Encryption disabled/ enabled",
       "ZCommunication  trouble",
       "ZTelco 1 fault",
       "ZTelco 2 fault",
       "ZLong Range Radio xmitter fault",
       "ZFailure to communicate event",
       "ZLoss of Radio supervision",
       "ZLoss of central polling",
       "ZLong Range Radio VSWR problem",
       "ZPeriodic Comm Test Fail /Restore",
       "Z",
       "ZNew Registration",
       "ZAuthorized  Substitution Registration",
       "ZUnauthorized  Substitution Registration",
       "ZModule Firmware Update Start/Finish",
       "ZModule Firmware Update Failed",
       "ZProtection loop",
       "ZProtection loop open",
       "ZProtection loop short",
       "ZFire trouble",
       "ZExit error alarm (zone)",
       "ZPanic zone trouble",
       "ZHoldup zone trouble",
       "ZSwinger Trouble",
       "ZCrosszone Trouble",
       "ZSensor trouble",
       "ZLoss of supervision  RF",
       "ZLoss of supervision  RPM",
       "ZSensor tamper",
       "ZRF low battery",
       "ZSmoke detector Hi sensitivity",
       "ZSmoke detector Low sensitivity",
       "ZIntrusion detector Hi sensitivity",
       "ZIntrusion detector Low sensitivity",
       "ZSensor selftest failure",
       "ZSensor Watch trouble",
       "ZDrift Compensation Error",
       "ZMaintenance Alert",
       "ZCO Detector needs replacement",
       "UOpen/Close",
       "UArmed AWAY",
       "UGroup O/C",
       "UAutomatic O/C",
       "ULate to O/C (Note: use 453 or 454 instead )",
       "UDeferred O/C (Obsolete do not use )",
       "UCancel",
       "URemote arm/disarm",
       "UQuick arm",
       "UKeyswitch O/C",
       "UCallback request made",
       "USuccessful  download/access",
       "UUnsuccessful access",
       "USystem shutdown command received",
       "UDialer shutdown command received",
       "ZSuccessful Upload",
       "URemote Cancel",
       "URemote Verify",
       "UAccess denied",
       "UAccess report by user",
       "ZForced Access",
       "UEgress Denied",
       "UEgress Granted",
       "ZAccess Door propped open",
       "ZAccess point Door Status Monitor trouble",
       "ZAccess point Request To Exit trouble",
       "UAccess program mode entry",
       "UAccess program mode exit",
       "UAccess threat level change",
       "ZAccess relay/trigger fail",
       "ZAccess RTE shunt",
       "ZAccess DSM shunt",
       "USecond Person Access",
       "UIrregular Access",
       "UArmed STAY",
       "UKeyswitch Armed STAY",
       "UArmed with System Trouble Override",
       "UException O/C",
       "UEarly O/C",
       "ULate O/C",
       "UFailed to Open",
       "UFailed to Close",
       "UAutoarm Failed",
       "UPartial Arm",
       "UExit Error (user)",
       "UUser on Premises",
       "URecent Close",
       "ZWrong Code Entry",
       "ULegal Code Entry",
       "URearm after Alarm",
       "UAutoarm Time Extended",
       "ZPanic Alarm Reset",
       "UService On/Off Premises",
       "ZAccess reader disable",
       "ZSounder/Relay  Disable",
       "ZBell 1 disable",
       "ZBell 2 disable",
       "ZAlarm relay disable",
       "ZTrouble relay disable",
       "ZReversing relay disable",
       "ZNotification Appliance Ckt. # 3 disable",
       "ZNotification Appliance Ckt. # 4 disable",
       "ZModule Added",
       "ZModule Removed",
       "ZDialer disabled",
       "ZRadio transmitter disabled",
       "ZRemote  Upload/Download disabled",
       "ZZone/Sensor bypass",
       "ZFire bypass",
       "Z24 Hour zone bypass",
       "ZBurg. Bypass",
       "UGroup bypass",
       "ZSwinger bypass",
       "ZAccess zone shunt",
       "ZAccess point bypass",
       "ZVault Bypass",
       "ZVent Zone Bypass",
       "ZManual trigger test report",
       "ZPeriodic test report",
       "ZPeriodic RF transmission",
       "UFire test",
       "ZStatus report to follow",
       "ZListenin to follow",
       "UWalk test mode",
       "ZPeriodic test  System Trouble Present",
       "ZVideo Xmitter active",
       "ZPoint tested OK",
       "ZPoint not tested",
       "ZIntrusion Zone Walk Tested",
       "ZFire Zone Walk Tested",
       "ZPanic Zone Walk Tested",
       "ZService Request",
       "ZEvent Log reset",
       "ZEvent Log 50% full",
       "ZEvent Log 90% full",
       "ZEvent Log overflow",
       "UTime/Date reset",
       "ZTime/Date inaccurate",
       "ZProgram mode entry",
       "ZProgram mode exit",
       "Z32 Hour Event log marker",
       "ZSchedule change",
       "ZException schedule change",
       "ZAccess schedule change",
       "ZSenior Watch Trouble",
       "ULatchkey Supervision",
       "ZRestricted Door Opened",
       "ZHelp Arrived",
       "ZAddition Help Needed",
       "ZAddition Help Cancel",
       "ZReserved for Ademco Use",
       "UReserved for Ademco Use",
       "UReserved for Ademco Use",
       "ZSystem Inactivity",
       "UUser Code X modified by Installer",
       "ZAuxiliary #3",
       "ZInstaller Test",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUser Assigned",
       "ZUnable to output signal (Derived Channel)",
       "ZSTU Controller down (Derived Channel)",
       "ZDownload Abort",
       "ZDownload Start/End",
       "ZDownload Interrupted",
       "ZDevice Flash Update Started/ Completed",
       "ZDevice Flash Update Failed",
       "ZAutoclose with Bypass",
       "ZBypass Closing",
       "ZFire Alarm Silence",
       "USupervisory Point test Start/End",
       "UHoldup test Start/End",
       "UBurg. Test Print Start/End",
       "USupervisory Test Print Start/End",
       "ZBurg. Diagnostics Start/End",
       "ZFire Diagnostics Start/End",
       "ZUntyped diagnostics",
       "UTrouble Closing (closed with burg. during exit)",
       "UAccess Denied Code Unknown",
       "ZSupervisory Point Alarm",
       "ZSupervisory Point Bypass",
       "ZSupervisory Point Trouble",
       "ZHoldup Point Bypass",
       "ZAC Failure for 4 hours",
       "ZOutput Trouble",
       "UUser code for event",
       "ULogoff",
       "ZCS Connection Failure",
       "ZRcvr Database Connection Fail/Restore",
       "ZLicense Expiration Notify",
       "Z1 and 1/3 Day No Read Log",
       "ZUnknown"
};

extern inline char * lrr_msg_lookup(int statusCode)
{
        switch (statusCode)
        {
        case 100:
          return (char *) msg_values[0];
        case 101:
          return (char *) msg_values[1];
        case 102:
          return (char *) msg_values[2];
        case 110:
          return (char *) msg_values[3];
        case 111:
          return (char *) msg_values[4];
        case 112:
          return (char *) msg_values[5];
        case 113:
          return (char *) msg_values[6];
        case 114:
          return (char *) msg_values[7];
        case 115:
          return (char *) msg_values[8];
        case 116:
          return (char *) msg_values[9];
        case 117:
          return (char *) msg_values[10];
        case 118:
          return (char *) msg_values[11];
        case 120:
          return (char *) msg_values[12];
        case 121:
          return (char *) msg_values[13];
        case 122:
          return (char *) msg_values[14];
        case 123:
          return (char *) msg_values[15];
        case 124:
          return (char *) msg_values[16];
        case 125:
          return (char *) msg_values[17];
        case 126:
          return (char *) msg_values[18];
        case 127:
          return (char *) msg_values[19];
        case 129:
          return (char *) msg_values[20];
        case 130:
          return (char *) msg_values[21];
        case 131:
          return (char *) msg_values[22];
        case 132:
          return (char *) msg_values[23];
        case 133:
          return (char *) msg_values[24];
        case 134:
          return (char *) msg_values[25];
        case 135:
          return (char *) msg_values[26];
        case 136:
          return (char *) msg_values[27];
        case 137:
          return (char *) msg_values[28];
        case 138:
          return (char *) msg_values[29];
        case 139:
          return (char *) msg_values[30];
        case 140:
          return (char *) msg_values[31];
        case 141:
          return (char *) msg_values[32];
        case 142:
          return (char *) msg_values[33];
        case 143:
          return (char *) msg_values[34];
        case 144:
          return (char *) msg_values[35];
        case 145:
          return (char *) msg_values[36];
        case 146:
          return (char *) msg_values[37];
        case 147:
          return (char *) msg_values[38];
        case 150:
          return (char *) msg_values[39];
        case 151:
          return (char *) msg_values[40];
        case 152:
          return (char *) msg_values[41];
        case 153:
          return (char *) msg_values[42];
        case 154:
          return (char *) msg_values[43];

        case 155:
          return (char *) msg_values[44];
        case 156:
          return (char *) msg_values[45];
        case 157:
          return (char *) msg_values[46];
        case 158:
          return (char *) msg_values[47];
        case 159:
          return (char *) msg_values[48];
        case 160:
          return (char *) msg_values[49];
        case 161:
          return (char *) msg_values[50];
        case 162:
          return (char *) msg_values[51];
        case 163:
          return (char *) msg_values[52];
        case 168:
          return (char *) msg_values[53];
        case 169:
          return (char *) msg_values[54];
        case 200:
          return (char *) msg_values[55];
        case 201:
          return (char *) msg_values[56];
        case 202:
          return (char *) msg_values[57];
        case 203:
          return (char *) msg_values[58];
        case 204:
          return (char *) msg_values[59];
        case 205:
          return (char *) msg_values[60];
        case 206:
          return (char *) msg_values[61];
        case 300:
          return (char *) msg_values[62];
        case 301:
          return (char *) msg_values[63];
        case 302:
          return (char *) msg_values[64];
        case 303:
          return (char *) msg_values[65];
        case 304:
          return (char *) msg_values[66];
        case 305:
          return (char *) msg_values[67];
        case 306:
          return (char *) msg_values[68];
        case 307:
          return (char *) msg_values[69];
        case 308:
          return (char *) msg_values[70];
        case 309:
          return (char *) msg_values[71];
        case 310:
          return (char *) msg_values[72];
        case 311:
          return (char *) msg_values[73];
        case 312:
          return (char *) msg_values[74];
        case 313:
          return (char *) msg_values[75];
        case 314:
          return (char *) msg_values[76];

        case 315:
          return (char *) msg_values[77];
        case 316:
          return (char *) msg_values[78];

        case 317:
          return (char *) msg_values[79];
        case 320:
          return (char *) msg_values[80];
        case 321:
          return (char *) msg_values[81];
        case 322:
          return (char *) msg_values[82];
        case 323:
          return (char *) msg_values[83];
        case 324:
          return (char *) msg_values[84];
        case 325:
          return (char *) msg_values[85];
        case 326:
          return (char *) msg_values[86];
        case 327:
          return (char *) msg_values[87];
        case 330:
          return (char *) msg_values[88];
        case 331:
          return (char *) msg_values[89];
        case 332:
          return (char *) msg_values[90];
        case 333:
          return (char *) msg_values[91];
        case 334:
          return (char *) msg_values[92];
        case 335:
          return (char *) msg_values[93];
        case 336:
          return (char *) msg_values[94];
        case 337:
          return (char *) msg_values[95];
        case 338:
          return (char *) msg_values[96];
        case 339:
          return (char *) msg_values[97];
        case 341:
          return (char *) msg_values[98];
        case 342:
          return (char *) msg_values[99];
        case 343:
          return (char *) msg_values[100];
        case 344:
          return (char *) msg_values[101];

        case 345:
          return (char *) msg_values[102];
        case 350:
          return (char *) msg_values[103];
        case 351:
          return (char *) msg_values[104];
        case 352:
          return (char *) msg_values[105];
        case 353:
          return (char *) msg_values[106];
        case 354:
          return (char *) msg_values[107];
        case 355:
          return (char *) msg_values[108];
        case 356:
          return (char *) msg_values[109];
        case 357:
          return (char *) msg_values[110];
        case 358:
          return (char *) msg_values[111];

        case 359:
          return (char *) msg_values[112];

        case 360:
          return (char *) msg_values[113];
        case 361:
          return (char *) msg_values[114];
        case 362:
          return (char *) msg_values[115];
        case 365:
          return (char *) msg_values[116];
        case 366:
          return (char *) msg_values[117];

        case 370:
          return (char *) msg_values[118];
        case 371:
          return (char *) msg_values[119];
        case 372:
          return (char *) msg_values[120];
        case 373:
          return (char *) msg_values[121];
        case 374:
          return (char *) msg_values[122];
        case 375:
          return (char *) msg_values[123];
        case 376:
          return (char *) msg_values[124];
        case 377:
          return (char *) msg_values[125];
        case 378:
          return (char *) msg_values[126];

        case 380:
          return (char *) msg_values[127];
        case 381:
          return (char *) msg_values[128];
        case 382:
          return (char *) msg_values[129];
        case 383:
          return (char *) msg_values[130];
        case 384:
          return (char *) msg_values[131];
        case 385:
          return (char *) msg_values[132];
        case 386:
          return (char *) msg_values[133];
        case 387:
          return (char *) msg_values[134];
        case 388:
          return (char *) msg_values[135];
        case 389:
          return (char *) msg_values[136];
        case 391:
          return (char *) msg_values[137];
        case 392:
          return (char *) msg_values[138];
        case 393:
          return (char *) msg_values[139];
        case 394:
          return (char *) msg_values[140];
        case 400:
          return (char *) msg_values[141];
        case 401:
          return (char *) msg_values[142];
        case 402:
          return (char *) msg_values[143];
        case 403:
          return (char *) msg_values[144];
        case 404:
          return (char *) msg_values[145];
        case 405:
          return (char *) msg_values[146];
        case 406:
          return (char *) msg_values[147];
        case 407:
          return (char *) msg_values[148];
        case 408:
          return (char *) msg_values[149];
        case 409:
          return (char *) msg_values[150];
        case 411:
          return (char *) msg_values[151];
        case 412:
          return (char *) msg_values[152];
        case 413:
          return (char *) msg_values[153];
        case 414:
          return (char *) msg_values[154];
        case 415:
          return (char *) msg_values[155];

        case 416:
          return (char *) msg_values[156];
        case 418:
          return (char *) msg_values[157];
        case 419:
          return (char *) msg_values[158];
        case 421:
          return (char *) msg_values[159];
        case 422:
          return (char *) msg_values[160];
        case 423:
          return (char *) msg_values[161];
        case 424:
          return (char *) msg_values[162];
        case 425:
          return (char *) msg_values[163];
        case 426:
          return (char *) msg_values[164];
        case 427:
          return (char *) msg_values[165];
        case 428:
          return (char *) msg_values[166];
        case 429:
          return (char *) msg_values[167];
        case 430:
          return (char *) msg_values[168];
        case 431:
          return (char *) msg_values[169];
        case 432:
          return (char *) msg_values[170];
        case 433:
          return (char *) msg_values[171];
        case 434:
          return (char *) msg_values[172];
        case 435:
          return (char *) msg_values[173];
        case 436:
          return (char *) msg_values[174];
        case 441:
          return (char *) msg_values[175];
        case 442:
          return (char *) msg_values[176];
        case 443:
          return (char *) msg_values[177];
        case 450:
          return (char *) msg_values[178];
        case 451:
          return (char *) msg_values[179];
        case 452:
          return (char *) msg_values[180];
        case 453:
          return (char *) msg_values[181];
        case 454:
          return (char *) msg_values[182];
        case 455:
          return (char *) msg_values[183];
        case 456:
          return (char *) msg_values[184];
        case 457:
          return (char *) msg_values[185];
        case 458:
          return (char *) msg_values[186];
        case 459:
          return (char *) msg_values[187];
        case 461:
          return (char *) msg_values[188];
        case 462:
          return (char *) msg_values[189];
        case 463:
          return (char *) msg_values[190];
        case 464:
          return (char *) msg_values[191];
        case 465:
          return (char *) msg_values[192];
        case 466:
          return (char *) msg_values[193];

        case 501:
          return (char *) msg_values[194];
        case 520:
          return (char *) msg_values[195];
        case 521:
          return (char *) msg_values[196];
        case 522:
          return (char *) msg_values[197];
        case 523:
          return (char *) msg_values[198];
        case 524:
          return (char *) msg_values[199];
        case 525:
          return (char *) msg_values[200];
        case 526:
          return (char *) msg_values[201];
        case 527:
          return (char *) msg_values[202];
        case 531:
          return (char *) msg_values[203];
        case 532:
          return (char *) msg_values[204];
        case 551:
          return (char *) msg_values[205];
        case 552:
          return (char *) msg_values[206];
        case 553:
          return (char *) msg_values[207];
        case 570:
          return (char *) msg_values[208];
        case 571:
          return (char *) msg_values[209];
        case 572:
          return (char *) msg_values[210];
        case 573:
          return (char *) msg_values[211];
        case 574:
          return (char *) msg_values[212];
        case 575:
          return (char *) msg_values[213];
        case 576:
          return (char *) msg_values[214];
        case 577:
          return (char *) msg_values[215];
        case 578:
          return (char *) msg_values[216];
        case 579:
          return (char *) msg_values[217];
        case 601:
          return (char *) msg_values[218];
        case 602:
          return (char *) msg_values[219];
        case 603:
          return (char *) msg_values[220];
        case 604:
          return (char *) msg_values[221];
        case 605:
          return (char *) msg_values[222];
        case 606:
          return (char *) msg_values[223];
        case 607:
          return (char *) msg_values[224];
        case 608:
          return (char *) msg_values[225];
        case 609:
          return (char *) msg_values[226];
        case 611:
          return (char *) msg_values[227];
        case 612:
          return (char *) msg_values[228];
        case 613:
          return (char *) msg_values[229];
        case 614:
          return (char *) msg_values[230];
        case 615:
          return (char *) msg_values[231];
        case 616:
          return (char *) msg_values[232];
        case 621:
          return (char *) msg_values[233];
        case 622:
          return (char *) msg_values[234];
        case 623:
          return (char *) msg_values[235];
        case 624:
          return (char *) msg_values[236];
        case 625:
          return (char *) msg_values[237];
        case 626:
          return (char *) msg_values[238];
        case 627:
          return (char *) msg_values[239];

        case 628:
          return (char *) msg_values[240];
        case 629:
          return (char *) msg_values[241];
        case 630:
          return (char *) msg_values[242];
        case 631:
          return (char *) msg_values[243];
        case 632:
          return (char *) msg_values[244];
        case 641:
          return (char *) msg_values[245];
        case 642:
          return (char *) msg_values[246];
        case 643:
          return (char *) msg_values[247];
        case 645:
          return (char *) msg_values[248];
        case 646:
          return (char *) msg_values[249];
        case 647:
          return (char *) msg_values[250];
        case 651:
          return (char *) msg_values[251];
        case 652:
          return (char *) msg_values[252];
        case 653:
          return (char *) msg_values[253];
        case 654:
          return (char *) msg_values[254];
        case 655:
          return (char *) msg_values[255];
        case 703:
          return (char *) msg_values[256];
        case 704:
          return (char *) msg_values[257];
        case 750:
          return (char *) msg_values[258];
        case 751:
          return (char *) msg_values[259];
        case 752:
          return (char *) msg_values[260];
        case 753:
          return (char *) msg_values[261];
        case 754:
          return (char *) msg_values[262];
        case 755:
          return (char *) msg_values[263];
        case 756:
          return (char *) msg_values[264];
        case 757:
          return (char *) msg_values[265];
        case 758:
          return (char *) msg_values[266];
        case 759:
          return (char *) msg_values[267];
        case 760:
          return (char *) msg_values[268];
        case 761:
          return (char *) msg_values[269];
        case 762:
          return (char *) msg_values[270];
        case 763:
          return (char *) msg_values[271];
        case 764:
          return (char *) msg_values[272];
        case 765:
          return (char *) msg_values[273];
        case 766:
          return (char *) msg_values[274];
        case 767:
          return (char *) msg_values[275];
        case 768:
          return (char *) msg_values[276];
        case 769:
          return (char *) msg_values[277];
        case 770:
          return (char *) msg_values[278];
        case 771:
          return (char *) msg_values[279];
        case 772:
          return (char *) msg_values[280];
        case 773:
          return (char *) msg_values[281];
        case 774:
          return (char *) msg_values[282];
        case 775:
          return (char *) msg_values[283];
        case 776:
          return (char *) msg_values[284];
        case 777:
          return (char *) msg_values[285];
        case 778:
          return (char *) msg_values[286];
        case 779:
          return (char *) msg_values[287];
        case 780:
          return (char *) msg_values[288];
        case 781:
          return (char *) msg_values[289];
        case 782:
          return (char *) msg_values[290];
        case 783:
          return (char *) msg_values[291];
        case 784:
          return (char *) msg_values[292];
        case 785:
          return (char *) msg_values[293];
        case 786:
          return (char *) msg_values[294];
        case 787:
          return (char *) msg_values[295];
        case 788:
          return (char *) msg_values[296];
        case 789:
          return (char *) msg_values[297];

        case 796:
          return (char *) msg_values[298];
        case 798:
          return (char *) msg_values[299];
        case 900:
          return (char *) msg_values[300];
        case 901:
          return (char *) msg_values[301];
        case 902:
          return (char *) msg_values[302];
        case 903:
          return (char *) msg_values[303];
        case 904:
          return (char *) msg_values[304];
        case 910:
          return (char *) msg_values[305];
        case 911:
          return (char *) msg_values[306];
        case 912:
          return (char *) msg_values[307];
        case 913:
          return (char *) msg_values[308];
        case 914:
          return (char *) msg_values[309];
        case 915:
          return (char *) msg_values[310];
        case 916:
          return (char *) msg_values[311];
        case 917:
          return (char *) msg_values[312];
        case 918:
          return (char *) msg_values[313];
        case 919:
          return (char *) msg_values[314];
        case 920:
          return (char *) msg_values[315];
        case 921:
          return (char *) msg_values[316];
        case 922:
          return (char *) msg_values[317];
        case 923:
          return (char *) msg_values[318];
        case 924:
          return (char *) msg_values[319];
        case 925:
          return (char *) msg_values[320];
        case 926:
          return (char *) msg_values[321];
        case 927:
          return (char *) msg_values[322];
        case 928:
          return (char *) msg_values[323];
        case 929:
          return (char *) msg_values[324];
        case 954:
          return (char *) msg_values[325];
        case 961:
          return (char *) msg_values[326];
        case 962:
          return (char *) msg_values[327];
        case 999:
          return (char *) msg_values[328];
        default:
          return (char *) msg_values[329];
    }
}

/*extern inline char * lrr_msg_lookup(int statusCode)
{
    switch (statusCode)
    {
        case 100:
          return "ZMedical";
        case 101:
          return "ZPersonal Emergency";
        case 102:
          return "ZFail to report in";
        case 110:
          return "ZFire";
        case 111:
          return "ZSmoke";
        case 112:
          return "ZCombustion";
        case 113:
          return "ZWater Flow";
        case 114:
          return "ZHeat";
        case 115:
          return "ZPull Station";
        case 116:
          return "ZDuct";
        case 117:
          return "ZFlame";
        case 118:
          return "ZNear Alarm";
        case 120:
          return "ZPanic";
        case 121:
          return "UDuress";
        case 122:
          return "ZSilent";
        case 123:
          return "ZAudible";
        case 124:
          return "ZDuress – Access granted";
        case 125:
          return "ZDuress – Egress granted";
        case 126:
          return "UHoldup suspicion print";
        case 127:
          return "URemote Silent Panic";
        case 129:
          return "ZPanic Verifier";
        case 130:
          return "ZBurglary";
        case 131:
          return "ZPerimeter";
        case 132:
          return "ZInterior";
        case 133:
          return "Z24 Hour (Safe)";
        case 134:
          return "ZEntry/Exit";
        case 135:
          return "ZDay/Night";
        case 136:
          return "ZOutdoor";
        case 137:
          return "ZTamper";
        case 138:
          return "ZNear alarm";
        case 139:
          return "ZIntrusion Verifier";
        case 140:
          return "ZGeneral Alarm";
        case 141:
          return "ZPolling loop open";
        case 142:
          return "ZPolling loop short";
        case 143:
          return "ZExpansion module failure";
        case 144:
          return "ZSensor tamper";
        case 145:
          return "ZExpansion module tamper";
        case 146:
          return "ZSilent Burglary";
        case 147:
          return "ZSensor Supervision Failure";
        case 150:
          return "Z24 Hour NonBurglary";
        case 151:
          return "ZGas detected";
        case 152:
          return "ZRefrigeration";
        case 153:
          return "ZLoss of heat";
        case 154:
          return "ZWater Leakage";

        case 155:
          return "ZFoil Break";
        case 156:
          return "ZDay Trouble";
        case 157:
          return "ZLow bottled gas level";
        case 158:
          return "ZHigh temp";
        case 159:
          return "ZLow temp";
        case 160:
          return "ZAwareness Zone Response";
        case 161:
          return "ZLoss of air flow";
        case 162:
          return "ZCarbon Monoxide detected";
        case 163:
          return "ZTank level";
        case 168:
          return "ZHigh Humidity";
        case 169:
          return "ZLow Humidity";
        case 200:
          return "ZFire Supervisory";
        case 201:
          return "ZLow water pressure";
        case 202:
          return "ZLow CO2";
        case 203:
          return "ZGate valve sensor";
        case 204:
          return "ZLow water level";
        case 205:
          return "ZPump activated";
        case 206:
          return "ZPump failure";
        case 300:
          return "ZSystem Trouble";
        case 301:
          return "ZAC Loss";
        case 302:
          return "ZLow system battery";
        case 303:
          return "ZRAM Checksum bad";
        case 304:
          return "ZROM checksum bad";
        case 305:
          return "ZSystem reset";
        case 306:
          return "ZPanel programming changed";
        case 307:
          return "ZSelftest failure";
        case 308:
          return "ZSystem shutdown";
        case 309:
          return "ZBattery test failure";
        case 310:
          return "ZGround fault";
        case 311:
          return "ZBattery Missing/Dead";
        case 312:
          return "ZPower Supply Overcurrent";
        case 313:
          return "UEngineer Reset";
        case 314:
          return "ZPrimary Power Supply Failure";

        case 315:
          return "ZSystem Trouble";
        case 316:
          return "ZSystem Tamper";

        case 317:
          return "ZControl Panel System Tamper";
        case 320:
          return "ZSounder/Relay";
        case 321:
          return "ZBell 1";
        case 322:
          return "ZBell 2";
        case 323:
          return "ZAlarm relay";
        case 324:
          return "ZTrouble relay";
        case 325:
          return "ZReversing relay";
        case 326:
          return "ZNotification Appliance Ckt. # 3";
        case 327:
          return "ZNotification Appliance Ckt. #4";
        case 330:
          return "ZSystem Peripheral trouble";
        case 331:
          return "ZPolling loop open";
        case 332:
          return "ZPolling loop short";
        case 333:
          return "ZExpansion module failure";
        case 334:
          return "ZRepeater failure";
        case 335:
          return "ZLocal printer out of paper";
        case 336:
          return "ZLocal printer failure";
        case 337:
          return "ZExp. Module DC Loss";
        case 338:
          return "ZExp. Module Low Batt.";
        case 339:
          return "ZExp. Module Reset";
        case 341:
          return "ZExp. Module Tamper";
        case 342:
          return "ZExp. Module AC Loss";
        case 343:
          return "ZExp. Module selftest fail";
        case 344:
          return "ZRF Receiver Jam Detect";

        case 345:
          return "ZAES Encryption disabled/ enabled";
        case 350:
          return "ZCommunication  trouble";
        case 351:
          return "ZTelco 1 fault";
        case 352:
          return "ZTelco 2 fault";
        case 353:
          return "ZLong Range Radio xmitter fault";
        case 354:
          return "ZFailure to communicate event";
        case 355:
          return "ZLoss of Radio supervision";
        case 356:
          return "ZLoss of central polling";
        case 357:
          return "ZLong Range Radio VSWR problem";
        case 358:
          return "ZPeriodic Comm Test Fail /Restore";

        case 359:
          return "Z";

        case 360:
          return "ZNew Registration";
        case 361:
          return "ZAuthorized  Substitution Registration";
        case 362:
          return "ZUnauthorized  Substitution Registration";
        case 365:
          return "ZModule Firmware Update Start/Finish";
        case 366:
          return "ZModule Firmware Update Failed";

        case 370:
          return "ZProtection loop";
        case 371:
          return "ZProtection loop open";
        case 372:
          return "ZProtection loop short";
        case 373:
          return "ZFire trouble";
        case 374:
          return "ZExit error alarm (zone)";
        case 375:
          return "ZPanic zone trouble";
        case 376:
          return "ZHoldup zone trouble";
        case 377:
          return "ZSwinger Trouble";
        case 378:
          return "ZCrosszone Trouble";

        case 380:
          return "ZSensor trouble";
        case 381:
          return "ZLoss of supervision  RF";
        case 382:
          return "ZLoss of supervision  RPM";
        case 383:
          return "ZSensor tamper";
        case 384:
          return "ZRF low battery";
        case 385:
          return "ZSmoke detector Hi sensitivity";
        case 386:
          return "ZSmoke detector Low sensitivity";
        case 387:
          return "ZIntrusion detector Hi sensitivity";
        case 388:
          return "ZIntrusion detector Low sensitivity";
        case 389:
          return "ZSensor selftest failure";
        case 391:
          return "ZSensor Watch trouble";
        case 392:
          return "ZDrift Compensation Error";
        case 393:
          return "ZMaintenance Alert";
        case 394:
          return "ZCO Detector needs replacement";
        case 400:
          return "UOpen/Close";
        case 401:
          return "UArmed AWAY";
        case 402:
          return "UGroup O/C";
        case 403:
          return "UAutomatic O/C";
        case 404:
          return "ULate to O/C (Note: use 453 or 454 instead )";
        case 405:
          return "UDeferred O/C (Obsolete do not use )";
        case 406:
          return "UCancel";
        case 407:
          return "URemote arm/disarm";
        case 408:
          return "UQuick arm";
        case 409:
          return "UKeyswitch O/C";
        case 411:
          return "UCallback request made";
        case 412:
          return "USuccessful  download/access";
        case 413:
          return "UUnsuccessful access";
        case 414:
          return "USystem shutdown command received";
        case 415:
          return "UDialer shutdown command received";

        case 416:
          return "ZSuccessful Upload";
        case 418:
          return "URemote Cancel";
        case 419:
          return "URemote Verify";
        case 421:
          return "UAccess denied";
        case 422:
          return "UAccess report by user";
        case 423:
          return "ZForced Access";
        case 424:
          return "UEgress Denied";
        case 425:
          return "UEgress Granted";
        case 426:
          return "ZAccess Door propped open";
        case 427:
          return "ZAccess point Door Status Monitor trouble";
        case 428:
          return "ZAccess point Request To Exit trouble";
        case 429:
          return "UAccess program mode entry";
        case 430:
          return "UAccess program mode exit";
        case 431:
          return "UAccess threat level change";
        case 432:
          return "ZAccess relay/trigger fail";
        case 433:
          return "ZAccess RTE shunt";
        case 434:
          return "ZAccess DSM shunt";
        case 435:
          return "USecond Person Access";
        case 436:
          return "UIrregular Access";
        case 441:
          return "UArmed STAY";
        case 442:
          return "UKeyswitch Armed STAY";
        case 443:
          return "UArmed with System Trouble Override";
        case 450:
          return "UException O/C";
        case 451:
          return "UEarly O/C";
        case 452:
          return "ULate O/C";
        case 453:
          return "UFailed to Open";
        case 454:
          return "UFailed to Close";
        case 455:
          return "UAutoarm Failed";
        case 456:
          return "UPartial Arm";
        case 457:
          return "UExit Error (user)";
        case 458:
          return "UUser on Premises";
        case 459:
          return "URecent Close";
        case 461:
          return "ZWrong Code Entry";
        case 462:
          return "ULegal Code Entry";
        case 463:
          return "URearm after Alarm";
        case 464:
          return "UAutoarm Time Extended";
        case 465:
          return "ZPanic Alarm Reset";
        case 466:
          return "UService On/Off Premises";

        case 501:
          return "ZAccess reader disable";
        case 520:
          return "ZSounder/Relay  Disable";
        case 521:
          return "ZBell 1 disable";
        case 522:
          return "ZBell 2 disable";
        case 523:
          return "ZAlarm relay disable";
        case 524:
          return "ZTrouble relay disable";
        case 525:
          return "ZReversing relay disable";
        case 526:
          return "ZNotification Appliance Ckt. # 3 disable";
        case 527:
          return "ZNotification Appliance Ckt. # 4 disable";
        case 531:
          return "ZModule Added";
        case 532:
          return "ZModule Removed";
        case 551:
          return "ZDialer disabled";
        case 552:
          return "ZRadio transmitter disabled";
        case 553:
          return "ZRemote  Upload/Download disabled";
        case 570:
          return "ZZone/Sensor bypass";
        case 571:
          return "ZFire bypass";
        case 572:
          return "Z24 Hour zone bypass";
        case 573:
          return "ZBurg. Bypass";
        case 574:
          return "UGroup bypass";
        case 575:
          return "ZSwinger bypass";
        case 576:
          return "ZAccess zone shunt";
        case 577:
          return "ZAccess point bypass";
        case 578:
          return "ZVault Bypass";
        case 579:
          return "ZVent Zone Bypass";
        case 601:
          return "ZManual trigger test report";
        case 602:
          return "ZPeriodic test report";
        case 603:
          return "ZPeriodic RF transmission";
        case 604:
          return "UFire test";
        case 605:
          return "ZStatus report to follow";
        case 606:
          return "ZListenin to follow";
        case 607:
          return "UWalk test mode";
        case 608:
          return "ZPeriodic test  System Trouble Present";
        case 609:
          return "ZVideo Xmitter active";
        case 611:
          return "ZPoint tested OK";
        case 612:
          return "ZPoint not tested";
        case 613:
          return "ZIntrusion Zone Walk Tested";
        case 614:
          return "ZFire Zone Walk Tested";
        case 615:
          return "ZPanic Zone Walk Tested";
        case 616:
          return "ZService Request";
        case 621:
          return "ZEvent Log reset";
        case 622:
          return "ZEvent Log 50% full";
        case 623:
          return "ZEvent Log 90% full";
        case 624:
          return "ZEvent Log overflow";
        case 625:
          return "UTime/Date reset";
        case 626:
          return "ZTime/Date inaccurate";
        case 627:
          return "ZProgram mode entry";

        case 628:
          return "ZProgram mode exit";
        case 629:
          return "Z32 Hour Event log marker";
        case 630:
          return "ZSchedule change";
        case 631:
          return "ZException schedule change";
        case 632:
          return "ZAccess schedule change";
        case 641:
          return "ZSenior Watch Trouble";
        case 642:
          return "ULatchkey Supervision";
        case 643:
          return "ZRestricted Door Opened";
        case 645:
          return "ZHelp Arrived";
        case 646:
          return "ZAddition Help Needed";
        case 647:
          return "ZAddition Help Cancel";
        case 651:
          return "ZReserved for Ademco Use";
        case 652:
          return "UReserved for Ademco Use";
        case 653:
          return "UReserved for Ademco Use";
        case 654:
          return "ZSystem Inactivity";
        case 655:
          return "UUser Code X modified by Installer";
        case 703:
          return "ZAuxiliary #3";
        case 704:
          return "ZInstaller Test";
        case 750:
          return "ZUser Assigned";
        case 751:
          return "ZUser Assigned";
        case 752:
          return "ZUser Assigned";
        case 753:
          return "ZUser Assigned";
        case 754:
          return "ZUser Assigned";
        case 755:
          return "ZUser Assigned";
        case 756:
          return "ZUser Assigned";
        case 757:
          return "ZUser Assigned";
        case 758:
          return "ZUser Assigned";
        case 759:
          return "ZUser Assigned";
        case 760:
          return "ZUser Assigned";
        case 761:
          return "ZUser Assigned";
        case 762:
          return "ZUser Assigned";
        case 763:
          return "ZUser Assigned";
        case 764:
          return "ZUser Assigned";
        case 765:
          return "ZUser Assigned";
        case 766:
          return "ZUser Assigned";
        case 767:
          return "ZUser Assigned";
        case 768:
          return "ZUser Assigned";
        case 769:
          return "ZUser Assigned";
        case 770:
          return "ZUser Assigned";
        case 771:
          return "ZUser Assigned";
        case 772:
          return "ZUser Assigned";
        case 773:
          return "ZUser Assigned";
        case 774:
          return "ZUser Assigned";
        case 775:
          return "ZUser Assigned";
        case 776:
          return "ZUser Assigned";
        case 777:
          return "ZUser Assigned";
        case 778:
          return "ZUser Assigned";
        case 779:
          return "ZUser Assigned";
        case 780:
          return "ZUser Assigned";
        case 781:
          return "ZUser Assigned";
        case 782:
          return "ZUser Assigned";
        case 783:
          return "ZUser Assigned";
        case 784:
          return "ZUser Assigned";
        case 785:
          return "ZUser Assigned";
        case 786:
          return "ZUser Assigned";
        case 787:
          return "ZUser Assigned";
        case 788:
          return "ZUser Assigned";
        case 789:
          return "ZUser Assigned";

        case 796:
          return "ZUnable to output signal (Derived Channel)";
        case 798:
          return "ZSTU Controller down (Derived Channel)";
        case 900:
          return "ZDownload Abort";
        case 901:
          return "ZDownload Start/End";
        case 902:
          return "ZDownload Interrupted";
        case 903:
          return "ZDevice Flash Update Started/ Completed";
        case 904:
          return "ZDevice Flash Update Failed";
        case 910:
          return "ZAutoclose with Bypass";
        case 911:
          return "ZBypass Closing";
        case 912:
          return "ZFire Alarm Silence";
        case 913:
          return "USupervisory Point test Start/End";
        case 914:
          return "UHoldup test Start/End";
        case 915:
          return "UBurg. Test Print Start/End";
        case 916:
          return "USupervisory Test Print Start/End";
        case 917:
          return "ZBurg. Diagnostics Start/End";
        case 918:
          return "ZFire Diagnostics Start/End";
        case 919:
          return "ZUntyped diagnostics";
        case 920:
          return "UTrouble Closing (closed with burg. during exit)";
        case 921:
          return "UAccess Denied Code Unknown";
        case 922:
          return "ZSupervisory Point Alarm";
        case 923:
          return "ZSupervisory Point Bypass";
        case 924:
          return "ZSupervisory Point Trouble";
        case 925:
          return "ZHoldup Point Bypass";
        case 926:
          return "ZAC Failure for 4 hours";
        case 927:
          return "ZOutput Trouble";
        case 928:
          return "UUser code for event";
        case 929:
          return "ULogoff";
        case 954:
          return "ZCS Connection Failure";
        case 961:
          return "ZRcvr Database Connection Fail/Restore";
        case 962:
          return "ZLicense Expiration Notify";
        case 999:
          return "Z1 and 1/3 Day No Read Log";
        default:
          return "ZUnknown";
    }
}*/