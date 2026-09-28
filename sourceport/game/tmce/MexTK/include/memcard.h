#ifndef MEX_H_MEMCARD
#define MEX_H_MEMCARD

#include "structs.h"
#include "datatypes.h"
#include "css.h"

#define MEMCARD_BANNER_SIZE 0x1800
#define MEMCARD_ICON_SIZE 0x400

enum MemCardStatus
{
    MEMCARD_STATUS_0,
    MEMCARD_STATUS_FILE_ALREADY_EXISTS,
    MEMCARD_STATUS_2,
    MEMCARD_STATUS_3,
    MEMCARD_STATUS_FILE_NOT_FOUND,
};

/*** Enums ***/
enum LanguageKind
{
    LANG_JPN,
    LANG_USA,
};

enum ScoreType
{
    SCORETYPE_KO,
    SCORETYPE_TIME,
};

/*** Structs ***/

// Aitch: This is a custom (non-melee) struct used by TM to save overlays.
// Is there a better place to put this?
typedef struct OverlaySave {
    u8 group;   // OverlayGroup
    u8 overlay; // idx into LabValues_OverlayColours
} OverlaySave;

struct Memcard /* native twin, generated */
{
    union {
        char _mex_native_size[68744];
        struct { int unk0; };
        struct { char _p4770[4]; int unk1; };
        struct { char _p4771[8]; int unk2; };
        struct { char _p4772[12]; int unk3; };
        struct { char _p4773[16]; int unk4; };
        struct { char _p4774[20]; int unk5; };
        struct { char _p4775[24]; int unk6; };
        struct { char _p4776[28]; int unk7; };
        struct { char _p4777[32]; int unk8; };
        struct { char _p4778[36]; int unk9; };
        struct { char _p4779[40]; int unk10; };
        struct { char _p4780[44]; int unk11; };
        struct { char _p4781[48]; int unk12; };
        struct { char _p4782[52]; int unk13; };
        struct { char _p4783[60]; int unk15; };
        struct { char _p4784[64]; int unk16; };
        struct { char _p4785[68]; int unk17; };
        struct { char _p4786[72]; int unk18; };
        struct { char _p4787[76]; int unk19; };
        struct { char _p4788[80]; int unk20; };
        struct { char _p4789[84]; int unk21; };
        struct { char _p4790[88]; int unk22; };
        struct { char _p4791[92]; int unk23; };
        struct { char _p4792[96]; int unk24; };
        struct { char _p4793[100]; int unk25; };
        struct { char _p4794[104]; int unk26; };
        struct { char _p4795[108]; int unk27; };
        struct { char _p4796[112]; int unk28; };
        struct { char _p4797[116]; int unk29; };
        struct { char _p4798[120]; int unk30; };
        struct { char _p4799[124]; int unk31; };
        struct { char _p4800[128]; int unk32; };
        struct { char _p4801[132]; int unk33; };
        struct { char _p4802[136]; int unk34; };
        struct { char _p4803[140]; int unk35; };
        struct { char _p4804[144]; int unk36; };
        struct { char _p4805[148]; int unk37; };
        struct { char _p4806[152]; int unk38; };
        struct { char _p4807[156]; int unk39; };
        struct { char _p4808[160]; int unk40; };
        struct { char _p4809[164]; int unk41; };
        struct { char _p4810[168]; int unk42; };
        struct { char _p4811[172]; int unk43; };
        struct { char _p4812[176]; int unk44; };
        struct { char _p4813[180]; int unk45; };
        struct { char _p4814[184]; int unk46; };
        struct { char _p4815[188]; int unk47; };
        struct { char _p4816[192]; int unk48; };
        struct { char _p4817[196]; int unk49; };
        struct { char _p4818[200]; int unk50; };
        struct { char _p4819[204]; int unk51; };
        struct { char _p4820[208]; int unk52; };
        struct { char _p4821[212]; int unk53; };
        struct { char _p4822[216]; int unk54; };
        struct { char _p4823[220]; int unk55; };
        struct { char _p4824[224]; int unk56; };
        struct { char _p4825[228]; int unk57; };
        struct { char _p4826[232]; int unk58; };
        struct { char _p4827[236]; int unk59; };
        struct { char _p4828[240]; int unk60; };
        struct { char _p4829[244]; int unk61; };
        struct { char _p4830[248]; int unk62; };
        struct { char _p4831[252]; int unk63; };
        struct { char _p4832[256]; int unk64; };
        struct { char _p4833[260]; int unk65; };
        struct { char _p4834[264]; int unk66; };
        struct { char _p4835[268]; int unk67; };
        struct { char _p4836[272]; int unk68; };
        struct { char _p4837[276]; int unk69; };
        struct { char _p4838[280]; int unk70; };
        struct { char _p4839[284]; int unk71; };
        struct { char _p4840[288]; int unk72; };
        struct { char _p4841[292]; int unk73; };
        struct { char _p4842[296]; int unk74; };
        struct { char _p4843[300]; int unk75; };
        struct { char _p4844[304]; int unk76; };
        struct { char _p4845[308]; int unk77; };
        struct { char _p4846[312]; int unk78; };
        struct { char _p4847[316]; int unk79; };
        struct { char _p4848[320]; int unk80; };
        struct { char _p4849[324]; int unk81; };
        struct { char _p4850[328]; int unk82; };
        struct { char _p4851[332]; int unk83; };
        struct { char _p4852[336]; int unk84; };
        struct { char _p4853[340]; int unk85; };
        struct { char _p4854[344]; int unk86; };
        struct { char _p4855[348]; int unk87; };
        struct { char _p4856[352]; int unk88; };
        struct { char _p4857[356]; int unk89; };
        struct { char _p4858[360]; int unk90; };
        struct { char _p4859[364]; int unk91; };
        struct { char _p4860[368]; int unk92; };
        struct { char _p4861[372]; int unk93; };
        struct { char _p4862[376]; int unk94; };
        struct { char _p4863[380]; int unk95; };
        struct { char _p4864[384]; int unk96; };
        struct { char _p4865[388]; int unk97; };
        struct { char _p4866[392]; int unk98; };
        struct { char _p4867[396]; int unk99; };
        struct { char _p4868[400]; int unk100; };
        struct { char _p4869[404]; int unk101; };
        struct { char _p4870[408]; int unk102; };
        struct { char _p4871[412]; int unk103; };
        struct { char _p4872[416]; int unk104; };
        struct { char _p4873[420]; int unk105; };
        struct { char _p4874[424]; int unk106; };
        struct { char _p4875[428]; int unk107; };
        struct { char _p4876[432]; int unk108; };
        struct { char _p4877[436]; int unk109; };
        struct { char _p4878[440]; int unk110; };
        struct { char _p4879[444]; int unk111; };
        struct { char _p4880[448]; int unk112; };
        struct { char _p4881[452]; int unk113; };
        struct { char _p4882[456]; int unk114; };
        struct { char _p4883[460]; int unk115; };
        struct { char _p4884[464]; int unk116; };
        struct { char _p4885[468]; int unk117; };
        struct { char _p4886[472]; int unk118; };
        struct { char _p4887[476]; int unk119; };
        struct { char _p4888[480]; int unk120; };
        struct { char _p4889[484]; int unk121; };
        struct { char _p4890[488]; int unk122; };
        struct { char _p4891[492]; int unk123; };
        struct { char _p4892[496]; int unk124; };
        struct { char _p4893[500]; int unk125; };
        struct { char _p4894[504]; int unk126; };
        struct { char _p4895[508]; int unk127; };
        struct { char _p4896[512]; int unk128; };
        struct { char _p4897[516]; int unk129; };
        struct { char _p4898[520]; int unk130; };
        struct { char _p4899[524]; int unk131; };
        struct { char _p4900[528]; int unk132; };
        struct { char _p4901[532]; int unk133; };
        struct { char _p4902[536]; int unk134; };
        struct { char _p4903[540]; int unk135; };
        struct { char _p4904[544]; int unk136; };
        struct { char _p4905[548]; int unk137; };
        struct { char _p4906[552]; int unk138; };
        struct { char _p4907[556]; int unk139; };
        struct { char _p4908[560]; int unk140; };
        struct { char _p4909[564]; int unk141; };
        struct { char _p4910[568]; int unk142; };
        struct { char _p4911[572]; int unk143; };
        struct { char _p4912[576]; int unk144; };
        struct { char _p4913[580]; int unk145; };
        struct { char _p4914[584]; int unk146; };
        struct { char _p4915[588]; int unk147; };
        struct { char _p4916[592]; int unk148; };
        struct { char _p4917[596]; int unk149; };
        struct { char _p4918[600]; int unk150; };
        struct { char _p4919[604]; int unk151; };
        struct { char _p4920[608]; int unk152; };
        struct { char _p4921[612]; int unk153; };
        struct { char _p4922[616]; int unk154; };
        struct { char _p4923[620]; int unk155; };
        struct { char _p4924[624]; int unk156; };
        struct { char _p4925[628]; int unk157; };
        struct { char _p4926[632]; int unk158; };
        struct { char _p4927[636]; int unk159; };
        struct { char _p4928[640]; int unk160; };
        struct { char _p4929[644]; int unk161; };
        struct { char _p4930[648]; int unk162; };
        struct { char _p4931[652]; int unk163; };
        struct { char _p4932[656]; int unk164; };
        struct { char _p4933[660]; int unk165; };
        struct { char _p4934[664]; int unk166; };
        struct { char _p4935[668]; int unk167; };
        struct { char _p4936[672]; int unk168; };
        struct { char _p4937[676]; int unk169; };
        struct { char _p4938[680]; int unk170; };
        struct { char _p4939[684]; int unk171; };
        struct { char _p4940[688]; int unk172; };
        struct { char _p4941[692]; int unk173; };
        struct { char _p4942[696]; int unk174; };
        struct { char _p4943[700]; int unk175; };
        struct { char _p4944[704]; int unk176; };
        struct { char _p4945[708]; int unk177; };
        struct { char _p4946[712]; int unk178; };
        struct { char _p4947[716]; int unk179; };
        struct { char _p4948[720]; int unk180; };
        struct { char _p4949[724]; int unk181; };
        struct { char _p4950[728]; int unk182; };
        struct { char _p4951[732]; int unk183; };
        struct { char _p4952[736]; int unk184; };
        struct { char _p4953[740]; int unk185; };
        struct { char _p4954[744]; int unk186; };
        struct { char _p4955[748]; int unk187; };
        struct { char _p4956[752]; int unk188; };
        struct { char _p4957[756]; int unk189; };
        struct { char _p4958[760]; int unk190; };
        struct { char _p4959[764]; int unk191; };
        struct { char _p4960[768]; int unk192; };
        struct { char _p4961[772]; int unk193; };
        struct { char _p4962[776]; int unk194; };
        struct { char _p4963[780]; int unk195; };
        struct { char _p4964[784]; int unk196; };
        struct { char _p4965[788]; int unk197; };
        struct { char _p4966[792]; int unk198; };
        struct { char _p4967[796]; int unk199; };
        struct { char _p4968[800]; int unk200; };
        struct { char _p4969[804]; int unk201; };
        struct { char _p4970[808]; int unk202; };
        struct { char _p4971[812]; int unk203; };
        struct { char _p4972[816]; int unk204; };
        struct { char _p4973[820]; int unk205; };
        struct { char _p4974[824]; int unk206; };
        struct { char _p4975[828]; int unk207; };
        struct { char _p4976[832]; int unk208; };
        struct { char _p4977[836]; int unk209; };
        struct { char _p4978[840]; int unk210; };
        struct { char _p4979[844]; int unk211; };
        struct { char _p4980[848]; int unk212; };
        struct { char _p4981[852]; int unk213; };
        struct { char _p4982[856]; int unk214; };
        struct { char _p4983[860]; int unk215; };
        struct { char _p4984[864]; int unk216; };
        struct { char _p4985[868]; int unk217; };
        struct { char _p4986[872]; int unk218; };
        struct { char _p4987[876]; int unk219; };
        struct { char _p4988[880]; int unk220; };
        struct { char _p4989[884]; int unk221; };
        struct { char _p4990[888]; int unk222; };
        struct { char _p4991[892]; int unk223; };
        struct { char _p4992[896]; int unk224; };
        struct { char _p4993[900]; int unk225; };
        struct { char _p4994[904]; int unk226; };
        struct { char _p4995[908]; int unk227; };
        struct { char _p4996[912]; int unk228; };
        struct { char _p4997[916]; int unk229; };
        struct { char _p4998[920]; int unk230; };
        struct { char _p4999[924]; int unk231; };
        struct { char _p5000[928]; int unk232; };
        struct { char _p5001[932]; int unk233; };
        struct { char _p5002[936]; int unk234; };
        struct { char _p5003[940]; int unk235; };
        struct { char _p5004[944]; int unk236; };
        struct { char _p5005[948]; int unk237; };
        struct { char _p5006[952]; int unk238; };
        struct { char _p5007[956]; int unk239; };
        struct { char _p5008[960]; int unk240; };
        struct { char _p5009[964]; int unk241; };
        struct { char _p5010[968]; int unk242; };
        struct { char _p5011[972]; int unk243; };
        struct { char _p5012[976]; int unk244; };
        struct { char _p5013[980]; int unk245; };
        struct { char _p5014[984]; int unk246; };
        struct { char _p5015[988]; int unk247; };
        struct { char _p5016[992]; int unk248; };
        struct { char _p5017[996]; int unk249; };
        struct { char _p5018[1000]; int unk250; };
        struct { char _p5019[1004]; int unk251; };
        struct { char _p5020[1008]; int unk252; };
        struct { char _p5021[1012]; int unk253; };
        struct { char _p5022[1016]; int unk254; };
        struct { char _p5023[1020]; int unk255; };
        struct { char _p5024[1024]; int unk256; };
        struct { char _p5025[1028]; int unk257; };
        struct { char _p5026[1032]; int unk258; };
        struct { char _p5027[1036]; int unk259; };
        struct { char _p5028[1040]; int unk260; };
        struct { char _p5029[1044]; int unk261; };
        struct { char _p5030[1048]; int unk262; };
        struct { char _p5031[1052]; int unk263; };
        struct { char _p5032[1056]; int unk264; };
        struct { char _p5033[1060]; int unk265; };
        struct { char _p5034[1064]; int unk266; };
        struct { char _p5035[1068]; int unk267; };
        struct { char _p5036[1072]; int unk268; };
        struct { char _p5037[1076]; int unk269; };
        struct { char _p5038[1080]; int unk270; };
        struct { char _p5039[1084]; int unk271; };
        struct { char _p5040[1088]; int unk272; };
        struct { char _p5041[1092]; int unk273; };
        struct { char _p5042[1096]; int unk274; };
        struct { char _p5043[1100]; int unk275; };
        struct { char _p5044[1104]; int unk276; };
        struct { char _p5045[1108]; int unk277; };
        struct { char _p5046[1112]; int unk278; };
        struct { char _p5047[1116]; int unk279; };
        struct { char _p5048[1120]; int unk280; };
        struct { char _p5049[1124]; int unk281; };
        struct { char _p5050[1128]; int unk282; };
        struct { char _p5051[1132]; int unk283; };
        struct { char _p5052[1136]; int unk284; };
        struct { char _p5053[1140]; int unk285; };
        struct { char _p5054[1144]; int unk286; };
        struct { char _p5055[1148]; int unk287; };
        struct { char _p5056[1152]; int unk288; };
        struct { char _p5057[1156]; int unk289; };
        struct { char _p5058[1160]; int unk290; };
        struct { char _p5059[1164]; int unk291; };
        struct { char _p5060[1168]; int unk292; };
        struct { char _p5061[1172]; int unk293; };
        struct { char _p5062[1176]; int unk294; };
        struct { char _p5063[1180]; int unk295; };
        struct { char _p5064[1184]; int unk296; };
        struct { char _p5065[1188]; int unk297; };
        struct { char _p5066[1192]; int unk298; };
        struct { char _p5067[1196]; int unk299; };
        struct { char _p5068[1200]; int unk300; };
        struct { char _p5069[1204]; int unk301; };
        struct { char _p5070[1208]; int unk302; };
        struct { char _p5071[1212]; int unk303; };
        struct { char _p5072[1216]; int unk304; };
        struct { char _p5073[1220]; int unk305; };
        struct { char _p5074[1224]; int unk306; };
        struct { char _p5075[1228]; int unk307; };
        struct { char _p5076[1232]; int unk308; };
        struct { char _p5077[1236]; int unk309; };
        struct { char _p5078[1240]; int unk310; };
        struct { char _p5079[1244]; int unk311; };
        struct { char _p5080[1248]; int unk312; };
        struct { char _p5081[1252]; int unk313; };
        struct { char _p5082[1256]; int unk314; };
        struct { char _p5083[1260]; int unk315; };
        struct { char _p5084[1264]; int unk316; };
        struct { char _p5085[1268]; int unk317; };
        struct { char _p5086[1272]; int unk318; };
        struct { char _p5087[1276]; int unk319; };
        struct { char _p5088[1280]; int unk320; };
        struct { char _p5089[1284]; int unk321; };
        struct { char _p5090[1288]; int unk322; };
        struct { char _p5091[1292]; int unk323; };
        struct { char _p5092[1296]; int unk324; };
        struct { char _p5093[1300]; int unk325; };
        struct { char _p5094[1304]; int unk326; };
        struct { char _p5095[1308]; int unk327; };
        struct { char _p5096[1312]; int unk328; };
        struct { char _p5097[1316]; int unk329; };
        struct { char _p5098[1320]; int unk330; };
        struct { char _p5099[1324]; int unk331; };
        struct { char _p5100[1328]; u8 fighter_prev; };
        struct { char _p5101[1329]; u8 x531; };
        struct { char _p5102[1330]; CSSBackup EventBackup; };
        struct { char _p5103[1340]; int unk335; };
        struct { char _p5104[1344]; int unk336; };
        struct { char _p5105[1348]; int unk337; };
        struct { char _p5106[1352]; int unk338; };
        struct { char _p5107[1356]; int unk339; };
        struct { char _p5108[1360]; int unk340; };
        struct { char _p5109[1364]; int unk341; };
        struct { char _p5110[1368]; int unk342; };
        struct { char _p5111[1372]; int unk343; };
        struct { char _p5112[1376]; int unk344; };
        struct { char _p5113[1380]; int unk345; };
        struct { char _p5114[1384]; int unk346; };
        struct { char _p5115[1388]; int unk347; };
        struct { char _p5116[1392]; int unk348; };
        struct { char _p5117[1396]; int unk349; };
        struct { char _p5118[1400]; int unk350; };
        struct { char _p5119[1404]; int unk351; };
        struct { char _p5120[1408]; int unk352; };
        struct { char _p5121[1412]; int unk353; };
        struct { char _p5122[1416]; int unk354; };
        struct { char _p5123[1424]; int unk356; };
        struct { char _p5124[1428]; int unk357; };
        struct { char _p5125[1432]; int unk358; };
        struct { char _p5126[1440]; int unk360; };
        struct { char _p5127[1444]; int unk361; };
        struct { char _p5128[1448]; int unk362; };
        struct { char _p5129[1452]; int unk363; };
        struct { char _p5130[1456]; int unk364; };
        struct { char _p5131[1460]; int unk365; };
        struct { char _p5132[1464]; int unk366; };
        struct { char _p5133[1468]; int unk367; };
        struct { char _p5134[1472]; int unk368; };
        struct { char _p5135[1476]; int unk369; };
        struct { char _p5136[1480]; int unk370; };
        struct { char _p5137[1484]; int unk371; };
        struct { char _p5138[1560]; int unk381; };
        struct { char _p5139[1568]; int unk382; };
        struct { char _p5140[1572]; int unk383; };
        struct { char _p5141[1576]; int unk384; };
        struct { char _p5142[1580]; int unk385; };
        struct { char _p5143[1584]; int unk386; };
        struct { char _p5144[1588]; int unk387; };
        struct { char _p5145[1592]; int unk388; };
        struct { char _p5146[1596]; int unk389; };
        struct { char _p5147[1600]; int unk390; };
        struct { char _p5148[1604]; int unk391; };
        struct { char _p5149[1608]; int unk392; };
        struct { char _p5150[1612]; int unk393; };
        struct { char _p5151[1616]; int unk394; };
        struct { char _p5152[1620]; int unk395; };
        struct { char _p5153[1624]; int unk396; };
        struct { char _p5154[1628]; int unk397; };
        struct { char _p5155[1632]; int unk398; };
        struct { char _p5156[1636]; int unk399; };
        struct { char _p5157[1640]; int unk400; };
        struct { char _p5158[1644]; int unk401; };
        struct { char _p5159[1648]; int unk402; };
        struct { char _p5160[1652]; int unk403; };
        struct { char _p5161[1656]; int unk404; };
        struct { char _p5162[1660]; int unk405; };
        struct { char _p5163[1664]; int unk406; };
        struct { char _p5164[1668]; int unk407; };
        struct { char _p5165[1672]; int unk408; };
        struct { char _p5166[1676]; int unk409; };
        struct { char _p5167[1680]; int unk410; };
        struct { char _p5168[1684]; int unk411; };
        struct { char _p5169[1688]; int unk412; };
        struct { char _p5170[1692]; int unk413; };
        struct { char _p5171[1696]; int unk414; };
        struct { char _p5172[1700]; int unk415; };
        struct { char _p5173[1704]; int unk416; };
        struct { char _p5174[1708]; int unk417; };
        struct { char _p5175[1712]; int unk418; };
        struct { char _p5176[1716]; int unk419; };
        struct { char _p5177[1720]; int unk420; };
        struct { char _p5178[1724]; int unk421; };
        struct { char _p5179[1728]; int unk422; };
        struct { char _p5180[1732]; int unk423; };
        struct { char _p5181[1736]; int unk424; };
        struct { char _p5182[1740]; int unk425; };
        struct { char _p5183[1744]; int unk426; };
        struct { char _p5184[1748]; int unk427; };
        struct { char _p5185[1752]; int unk428; };
        struct { char _p5186[1756]; int unk429; };
        struct { char _p5187[1760]; int unk430; };
        struct { char _p5188[1764]; int unk431; };
        struct { char _p5189[1768]; int unk432; };
        struct { char _p5190[1772]; int unk433; };
        struct { char _p5191[1776]; int unk434; };
        struct { char _p5192[1780]; int unk435; };
        struct { char _p5193[1784]; int unk436; };
        struct { char _p5194[1788]; int unk437; };
        struct { char _p5195[1792]; int unk438; };
        struct { char _p5196[1800]; int unk440; };
        struct { char _p5197[1804]; int unk441; };
        struct { char _p5198[1808]; int unk442; };
        struct { char _p5199[1812]; int unk443; };
        struct { char _p5200[1816]; int unk444; };
        struct { char _p5201[1820]; int unk445; };
        struct { char _p5202[1824]; int unk446; };
        struct { char _p5203[1828]; int unk447; };
        struct { char _p5204[1832]; int unk448; };
        struct { char _p5205[1836]; int unk449; };
        struct { char _p5206[1840]; int unk450; };
        struct { char _p5207[1844]; int unk451; };
        struct { char _p5208[1920]; int unk461; };
        struct { char _p5209[1928]; int unk462; };
        struct { char _p5210[1932]; int unk463; };
        struct { char _p5211[1936]; int unk464; };
        struct { char _p5212[1940]; int unk465; };
        struct { char _p5213[1944]; int unk466; };
        struct { char _p5214[1948]; int unk467; };
        struct { char _p5215[1952]; int unk468; };
        struct { char _p5216[1956]; int unk469; };
        struct { char _p5217[1960]; int unk470; };
        struct { char _p5218[1964]; int unk471; };
        struct { char _p5219[1968]; int unk472; };
        struct { char _p5220[1972]; int unk473; };
        struct { char _p5221[1976]; int unk474; };
        struct { char _p5222[1980]; int unk475; };
        struct { char _p5223[1984]; int unk476; };
        struct { char _p5224[1988]; int unk477; };
        struct { char _p5225[1992]; int unk478; };
        struct { char _p5226[1996]; int unk479; };
        struct { char _p5227[2000]; int unk480; };
        struct { char _p5228[2004]; int unk481; };
        struct { char _p5229[2008]; int unk482; };
        struct { char _p5230[2012]; int unk483; };
        struct { char _p5231[2016]; int unk484; };
        struct { char _p5232[2020]; int unk485; };
        struct { char _p5233[2024]; int unk486; };
        struct { char _p5234[2028]; int unk487; };
        struct { char _p5235[2032]; int unk488; };
        struct { char _p5236[2036]; int unk489; };
        struct { char _p5237[2040]; int unk490; };
        struct { char _p5238[2044]; int unk491; };
        struct { char _p5239[2048]; int unk492; };
        struct { char _p5240[2052]; int unk493; };
        struct { char _p5241[2056]; int unk494; };
        struct { char _p5242[2060]; int unk495; };
        struct { char _p5243[2064]; int unk496; };
        struct { char _p5244[2068]; int unk497; };
        struct { char _p5245[2072]; int unk498; };
        struct { char _p5246[2076]; int unk499; };
        struct { char _p5247[2080]; int unk500; };
        struct { char _p5248[2084]; int unk501; };
        struct { char _p5249[2088]; int unk502; };
        struct { char _p5250[2092]; int unk503; };
        struct { char _p5251[2096]; int unk504; };
        struct { char _p5252[2100]; int unk505; };
        struct { char _p5253[2104]; int unk506; };
        struct { char _p5254[2108]; int unk507; };
        struct { char _p5255[2112]; int unk508; };
        struct { char _p5256[2116]; int unk509; };
        struct { char _p5257[2120]; int unk510; };
        struct { char _p5258[2124]; int unk511; };
        struct { char _p5259[2128]; int unk512; };
        struct { char _p5260[2132]; int unk513; };
        struct { char _p5261[2136]; int unk514; };
        struct { char _p5262[2140]; int unk515; };
        struct { char _p5263[2144]; int unk516; };
        struct { char _p5264[2148]; int unk517; };
        struct { char _p5265[2152]; int unk518; };
        struct { char _p5266[2160]; int unk520; };
        struct { char _p5267[2164]; int unk521; };
        struct { char _p5268[2168]; int unk522; };
        struct { char _p5269[2172]; int unk523; };
        struct { char _p5270[2176]; int unk524; };
        struct { char _p5271[2180]; int unk525; };
        struct { char _p5272[2184]; int unk526; };
        struct { char _p5273[2188]; int unk527; };
        struct { char _p5274[2192]; int unk528; };
        struct { char _p5275[2196]; int unk529; };
        struct { char _p5276[2200]; int unk530; };
        struct { char _p5277[2204]; int unk531; };
        struct { char _p5278[2280]; int unk541; };
        struct { char _p5279[2288]; int unk542; };
        struct { char _p5280[2292]; int unk543; };
        struct { char _p5281[2296]; int unk544; };
        struct { char _p5282[2300]; int unk545; };
        struct { char _p5283[2304]; int unk546; };
        struct { char _p5284[2308]; int unk547; };
        struct { char _p5285[2312]; int unk548; };
        struct { char _p5286[2316]; int unk549; };
        struct { char _p5287[2320]; int unk550; };
        struct { char _p5288[2324]; int unk551; };
        struct { char _p5289[2328]; int unk552; };
        struct { char _p5290[2332]; int unk553; };
        struct { char _p5291[2336]; int unk554; };
        struct { char _p5292[2340]; int unk555; };
        struct { char _p5293[2344]; int unk556; };
        struct { char _p5294[2348]; int unk557; };
        struct { char _p5295[2352]; int unk558; };
        struct { char _p5296[2356]; int unk559; };
        struct { char _p5297[2360]; int unk560; };
        struct { char _p5298[2364]; int unk561; };
        struct { char _p5299[2368]; int unk562; };
        struct { char _p5300[2372]; int unk563; };
        struct { char _p5301[2376]; int unk564; };
        struct { char _p5302[2380]; int unk565; };
        struct { char _p5303[2384]; int unk566; };
        struct { char _p5304[2388]; int unk567; };
        struct { char _p5305[2392]; int unk568; };
        struct { char _p5306[2396]; int unk569; };
        struct { char _p5307[2400]; int unk570; };
        struct { char _p5308[2404]; int unk571; };
        struct { char _p5309[2408]; int unk572; };
        struct { char _p5310[2412]; int unk573; };
        struct { char _p5311[2416]; int unk574; };
        struct { char _p5312[2420]; int unk575; };
        struct { char _p5313[2424]; int unk576; };
        struct { char _p5314[2428]; int unk577; };
        struct { char _p5315[2432]; int unk578; };
        struct { char _p5316[2436]; int unk579; };
        struct { char _p5317[2440]; int unk580; };
        struct { char _p5318[2444]; int unk581; };
        struct { char _p5319[2448]; int unk582; };
        struct { char _p5320[2452]; int unk583; };
        struct { char _p5321[2456]; int unk584; };
        struct { char _p5322[2460]; int unk585; };
        struct { char _p5323[2464]; int unk586; };
        struct { char _p5324[2468]; int unk587; };
        struct { char _p5325[2472]; int unk588; };
        struct { char _p5326[2476]; int unk589; };
        struct { char _p5327[2480]; int unk590; };
        struct { char _p5328[2484]; int unk591; };
        struct { char _p5329[2488]; int unk592; };
        struct { char _p5330[2492]; int unk593; };
        struct { char _p5331[2496]; int unk594; };
        struct { char _p5332[2500]; int unk595; };
        struct { char _p5333[2504]; int unk596; };
        struct { char _p5334[2508]; int unk597; };
        struct { char _p5335[2512]; int unk598; };
        struct { char _p5336[2520]; int unk600; };
        struct { char _p5337[2524]; int unk601; };
        struct { char _p5338[2528]; int unk602; };
        struct { char _p5339[2532]; int unk603; };
        struct { char _p5340[2536]; int unk604; };
        struct { char _p5341[2540]; int unk605; };
        struct { char _p5342[2544]; int unk606; };
        struct { char _p5343[2548]; int unk607; };
        struct { char _p5344[2552]; int unk608; };
        struct { char _p5345[2556]; int unk609; };
        struct { char _p5346[2560]; int unk610; };
        struct { char _p5347[2564]; int unk611; };
        struct { char _p5348[2640]; int unk621; };
        struct { char _p5349[2648]; int unk622; };
        struct { char _p5350[2652]; int unk623; };
        struct { char _p5351[2656]; int unk624; };
        struct { char _p5352[2660]; int unk625; };
        struct { char _p5353[2664]; int unk626; };
        struct { char _p5354[2668]; int unk627; };
        struct { char _p5355[2672]; int unk628; };
        struct { char _p5356[2676]; int unk629; };
        struct { char _p5357[2680]; int unk630; };
        struct { char _p5358[2684]; int unk631; };
        struct { char _p5359[2688]; int unk632; };
        struct { char _p5360[2692]; int unk633; };
        struct { char _p5361[2696]; int unk634; };
        struct { char _p5362[2700]; int unk635; };
        struct { char _p5363[2704]; int unk636; };
        struct { char _p5364[2708]; int unk637; };
        struct { char _p5365[2712]; int unk638; };
        struct { char _p5366[2716]; int unk639; };
        struct { char _p5367[2720]; int unk640; };
        struct { char _p5368[2724]; int unk641; };
        struct { char _p5369[2728]; int unk642; };
        struct { char _p5370[2732]; int unk643; };
        struct { char _p5371[2736]; int unk644; };
        struct { char _p5372[2740]; int unk645; };
        struct { char _p5373[2744]; int unk646; };
        struct { char _p5374[2748]; int unk647; };
        struct { char _p5375[2752]; int unk648; };
        struct { char _p5376[2756]; int unk649; };
        struct { char _p5377[2760]; int unk650; };
        struct { char _p5378[2764]; int unk651; };
        struct { char _p5379[2768]; int unk652; };
        struct { char _p5380[2772]; int unk653; };
        struct { char _p5381[2776]; int unk654; };
        struct { char _p5382[2780]; int unk655; };
        struct { char _p5383[2784]; int unk656; };
        struct { char _p5384[2788]; int unk657; };
        struct { char _p5385[2792]; int unk658; };
        struct { char _p5386[2796]; int unk659; };
        struct { char _p5387[2800]; int unk660; };
        struct { char _p5388[2804]; int unk661; };
        struct { char _p5389[2808]; int unk662; };
        struct { char _p5390[2812]; int unk663; };
        struct { char _p5391[2816]; int unk664; };
        struct { char _p5392[2820]; int unk665; };
        struct { char _p5393[2824]; int unk666; };
        struct { char _p5394[2828]; int unk667; };
        struct { char _p5395[2832]; int unk668; };
        struct { char _p5396[2836]; int unk669; };
        struct { char _p5397[2840]; int unk670; };
        struct { char _p5398[2844]; int unk671; };
        struct { char _p5399[2848]; int unk672; };
        struct { char _p5400[2852]; int unk673; };
        struct { char _p5401[2856]; int unk674; };
        struct { char _p5402[2860]; int unk675; };
        struct { char _p5403[2864]; int unk676; };
        struct { char _p5404[2868]; int unk677; };
        struct { char _p5405[2872]; int unk678; };
        struct { char _p5406[2880]; int unk680; };
        struct { char _p5407[2884]; int unk681; };
        struct { char _p5408[2888]; int unk682; };
        struct { char _p5409[2892]; int unk683; };
        struct { char _p5410[2896]; int unk684; };
        struct { char _p5411[2900]; int unk685; };
        struct { char _p5412[2904]; int unk686; };
        struct { char _p5413[2908]; int unk687; };
        struct { char _p5414[2912]; int unk688; };
        struct { char _p5415[2916]; int unk689; };
        struct { char _p5416[2920]; int unk690; };
        struct { char _p5417[2924]; int unk691; };
        struct { char _p5418[3000]; int unk701; };
        struct { char _p5419[3008]; int unk702; };
        struct { char _p5420[3012]; int unk703; };
        struct { char _p5421[3016]; int unk704; };
        struct { char _p5422[3020]; int unk705; };
        struct { char _p5423[3024]; int unk706; };
        struct { char _p5424[3028]; int unk707; };
        struct { char _p5425[3032]; int unk708; };
        struct { char _p5426[3036]; int unk709; };
        struct { char _p5427[3040]; int unk710; };
        struct { char _p5428[3044]; int unk711; };
        struct { char _p5429[3048]; int unk712; };
        struct { char _p5430[3052]; int unk713; };
        struct { char _p5431[3056]; int unk714; };
        struct { char _p5432[3060]; int unk715; };
        struct { char _p5433[3064]; int unk716; };
        struct { char _p5434[3068]; int unk717; };
        struct { char _p5435[3072]; int unk718; };
        struct { char _p5436[3076]; int unk719; };
        struct { char _p5437[3080]; int unk720; };
        struct { char _p5438[3084]; int unk721; };
        struct { char _p5439[3088]; int unk722; };
        struct { char _p5440[3092]; int unk723; };
        struct { char _p5441[3096]; int unk724; };
        struct { char _p5442[3100]; int unk725; };
        struct { char _p5443[3104]; int unk726; };
        struct { char _p5444[3108]; int unk727; };
        struct { char _p5445[3112]; int unk728; };
        struct { char _p5446[3116]; int unk729; };
        struct { char _p5447[3120]; int unk730; };
        struct { char _p5448[3124]; int unk731; };
        struct { char _p5449[3128]; int unk732; };
        struct { char _p5450[3132]; int unk733; };
        struct { char _p5451[3136]; int unk734; };
        struct { char _p5452[3140]; int unk735; };
        struct { char _p5453[3144]; int unk736; };
        struct { char _p5454[3148]; int unk737; };
        struct { char _p5455[3152]; int unk738; };
        struct { char _p5456[3156]; int unk739; };
        struct { char _p5457[3160]; int unk740; };
        struct { char _p5458[3164]; int unk741; };
        struct { char _p5459[3168]; int unk742; };
        struct { char _p5460[3172]; int unk743; };
        struct { char _p5461[3176]; int unk744; };
        struct { char _p5462[3180]; int unk745; };
        struct { char _p5463[3184]; int unk746; };
        struct { char _p5464[3188]; int unk747; };
        struct { char _p5465[3192]; int unk748; };
        struct { char _p5466[3196]; int unk749; };
        struct { char _p5467[3200]; int unk750; };
        struct { char _p5468[3204]; int unk751; };
        struct { char _p5469[3208]; int unk752; };
        struct { char _p5470[3212]; int unk753; };
        struct { char _p5471[3216]; int unk754; };
        struct { char _p5472[3220]; int unk755; };
        struct { char _p5473[3224]; int unk756; };
        struct { char _p5474[3228]; int unk757; };
        struct { char _p5475[3232]; int unk758; };
        struct { char _p5476[3240]; int unk760; };
        struct { char _p5477[3244]; int unk761; };
        struct { char _p5478[3248]; int unk762; };
        struct { char _p5479[3252]; int unk763; };
        struct { char _p5480[3256]; int unk764; };
        struct { char _p5481[3260]; int unk765; };
        struct { char _p5482[3264]; int unk766; };
        struct { char _p5483[3268]; int unk767; };
        struct { char _p5484[3272]; int unk768; };
        struct { char _p5485[3276]; int unk769; };
        struct { char _p5486[3280]; int unk770; };
        struct { char _p5487[3284]; int unk771; };
        struct { char _p5488[3360]; int unk781; };
        struct { char _p5489[3368]; int unk782; };
        struct { char _p5490[3372]; int unk783; };
        struct { char _p5491[3376]; int unk784; };
        struct { char _p5492[3380]; int unk785; };
        struct { char _p5493[3384]; int unk786; };
        struct { char _p5494[3388]; int unk787; };
        struct { char _p5495[3392]; int unk788; };
        struct { char _p5496[3396]; int unk789; };
        struct { char _p5497[3400]; int unk790; };
        struct { char _p5498[3404]; int unk791; };
        struct { char _p5499[3408]; int unk792; };
        struct { char _p5500[3412]; int unk793; };
        struct { char _p5501[3416]; int unk794; };
        struct { char _p5502[3420]; int unk795; };
        struct { char _p5503[3424]; int unk796; };
        struct { char _p5504[3428]; int unk797; };
        struct { char _p5505[3432]; int unk798; };
        struct { char _p5506[3436]; int unk799; };
        struct { char _p5507[3440]; int unk800; };
        struct { char _p5508[3444]; int unk801; };
        struct { char _p5509[3448]; int unk802; };
        struct { char _p5510[3452]; int unk803; };
        struct { char _p5511[3456]; int unk804; };
        struct { char _p5512[3460]; int unk805; };
        struct { char _p5513[3464]; int unk806; };
        struct { char _p5514[3468]; int unk807; };
        struct { char _p5515[3472]; int unk808; };
        struct { char _p5516[3476]; int unk809; };
        struct { char _p5517[3480]; int unk810; };
        struct { char _p5518[3484]; int unk811; };
        struct { char _p5519[3488]; int unk812; };
        struct { char _p5520[3492]; int unk813; };
        struct { char _p5521[3496]; int unk814; };
        struct { char _p5522[3500]; int unk815; };
        struct { char _p5523[3504]; int unk816; };
        struct { char _p5524[3508]; int unk817; };
        struct { char _p5525[3512]; int unk818; };
        struct { char _p5526[3516]; int unk819; };
        struct { char _p5527[3520]; int unk820; };
        struct { char _p5528[3524]; int unk821; };
        struct { char _p5529[3528]; int unk822; };
        struct { char _p5530[3532]; int unk823; };
        struct { char _p5531[3536]; int unk824; };
        struct { char _p5532[3540]; int unk825; };
        struct { char _p5533[3544]; int unk826; };
        struct { char _p5534[3548]; int unk827; };
        struct { char _p5535[3552]; int unk828; };
        struct { char _p5536[3556]; int unk829; };
        struct { char _p5537[3560]; int unk830; };
        struct { char _p5538[3564]; int unk831; };
        struct { char _p5539[3568]; int unk832; };
        struct { char _p5540[3572]; int unk833; };
        struct { char _p5541[3576]; int unk834; };
        struct { char _p5542[3580]; int unk835; };
        struct { char _p5543[3584]; int unk836; };
        struct { char _p5544[3588]; int unk837; };
        struct { char _p5545[3592]; int unk838; };
        struct { char _p5546[3600]; int unk840; };
        struct { char _p5547[3604]; int unk841; };
        struct { char _p5548[3608]; int unk842; };
        struct { char _p5549[3612]; int unk843; };
        struct { char _p5550[3616]; int unk844; };
        struct { char _p5551[3620]; int unk845; };
        struct { char _p5552[3624]; int unk846; };
        struct { char _p5553[3628]; int unk847; };
        struct { char _p5554[3632]; int unk848; };
        struct { char _p5555[3636]; int unk849; };
        struct { char _p5556[3640]; int unk850; };
        struct { char _p5557[3644]; int unk851; };
        struct { char _p5558[3720]; int unk861; };
        struct { char _p5559[3728]; int unk862; };
        struct { char _p5560[3732]; int unk863; };
        struct { char _p5561[3736]; int unk864; };
        struct { char _p5562[3740]; int unk865; };
        struct { char _p5563[3744]; int unk866; };
        struct { char _p5564[3748]; int unk867; };
        struct { char _p5565[3752]; int unk868; };
        struct { char _p5566[3756]; int unk869; };
        struct { char _p5567[3760]; int unk870; };
        struct { char _p5568[3764]; int unk871; };
        struct { char _p5569[3768]; int unk872; };
        struct { char _p5570[3772]; int unk873; };
        struct { char _p5571[3776]; int unk874; };
        struct { char _p5572[3780]; int unk875; };
        struct { char _p5573[3784]; int unk876; };
        struct { char _p5574[3788]; int unk877; };
        struct { char _p5575[3792]; int unk878; };
        struct { char _p5576[3796]; int unk879; };
        struct { char _p5577[3800]; int unk880; };
        struct { char _p5578[3804]; int unk881; };
        struct { char _p5579[3808]; int unk882; };
        struct { char _p5580[3812]; int unk883; };
        struct { char _p5581[3816]; int unk884; };
        struct { char _p5582[3820]; int unk885; };
        struct { char _p5583[3824]; int unk886; };
        struct { char _p5584[3828]; int unk887; };
        struct { char _p5585[3832]; int unk888; };
        struct { char _p5586[3836]; int unk889; };
        struct { char _p5587[3840]; int unk890; };
        struct { char _p5588[3844]; int unk891; };
        struct { char _p5589[3848]; int unk892; };
        struct { char _p5590[3852]; int unk893; };
        struct { char _p5591[3856]; int unk894; };
        struct { char _p5592[3860]; int unk895; };
        struct { char _p5593[3864]; int unk896; };
        struct { char _p5594[3868]; int unk897; };
        struct { char _p5595[3872]; int unk898; };
        struct { char _p5596[3876]; int unk899; };
        struct { char _p5597[3880]; int unk900; };
        struct { char _p5598[3884]; int unk901; };
        struct { char _p5599[3888]; int unk902; };
        struct { char _p5600[3892]; int unk903; };
        struct { char _p5601[3896]; int unk904; };
        struct { char _p5602[3900]; int unk905; };
        struct { char _p5603[3904]; int unk906; };
        struct { char _p5604[3908]; int unk907; };
        struct { char _p5605[3912]; int unk908; };
        struct { char _p5606[3916]; int unk909; };
        struct { char _p5607[3920]; int unk910; };
        struct { char _p5608[3924]; int unk911; };
        struct { char _p5609[3928]; int unk912; };
        struct { char _p5610[3932]; int unk913; };
        struct { char _p5611[3936]; int unk914; };
        struct { char _p5612[3940]; int unk915; };
        struct { char _p5613[3944]; int unk916; };
        struct { char _p5614[3948]; int unk917; };
        struct { char _p5615[3952]; int unk918; };
        struct { char _p5616[3960]; int unk920; };
        struct { char _p5617[3964]; int unk921; };
        struct { char _p5618[3968]; int unk922; };
        struct { char _p5619[3972]; int unk923; };
        struct { char _p5620[3976]; int unk924; };
        struct { char _p5621[3980]; int unk925; };
        struct { char _p5622[3984]; int unk926; };
        struct { char _p5623[3988]; int unk927; };
        struct { char _p5624[3992]; int unk928; };
        struct { char _p5625[3996]; int unk929; };
        struct { char _p5626[4000]; int unk930; };
        struct { char _p5627[4004]; int unk931; };
        struct { char _p5628[4080]; int unk941; };
        struct { char _p5629[4088]; int unk942; };
        struct { char _p5630[4092]; int unk943; };
        struct { char _p5631[4096]; int unk944; };
        struct { char _p5632[4100]; int unk945; };
        struct { char _p5633[4104]; int unk946; };
        struct { char _p5634[4108]; int unk947; };
        struct { char _p5635[4112]; int unk948; };
        struct { char _p5636[4116]; int unk949; };
        struct { char _p5637[4120]; int unk950; };
        struct { char _p5638[4124]; int unk951; };
        struct { char _p5639[4128]; int unk952; };
        struct { char _p5640[4132]; int unk953; };
        struct { char _p5641[4136]; int unk954; };
        struct { char _p5642[4140]; int unk955; };
        struct { char _p5643[4144]; int unk956; };
        struct { char _p5644[4148]; int unk957; };
        struct { char _p5645[4152]; int unk958; };
        struct { char _p5646[4156]; int unk959; };
        struct { char _p5647[4160]; int unk960; };
        struct { char _p5648[4164]; int unk961; };
        struct { char _p5649[4168]; int unk962; };
        struct { char _p5650[4172]; int unk963; };
        struct { char _p5651[4176]; int unk964; };
        struct { char _p5652[4180]; int unk965; };
        struct { char _p5653[4184]; int unk966; };
        struct { char _p5654[4188]; int unk967; };
        struct { char _p5655[4192]; int unk968; };
        struct { char _p5656[4196]; int unk969; };
        struct { char _p5657[4200]; int unk970; };
        struct { char _p5658[4204]; int unk971; };
        struct { char _p5659[4208]; int unk972; };
        struct { char _p5660[4212]; int unk973; };
        struct { char _p5661[4216]; int unk974; };
        struct { char _p5662[4220]; int unk975; };
        struct { char _p5663[4224]; int unk976; };
        struct { char _p5664[4228]; int unk977; };
        struct { char _p5665[4232]; int unk978; };
        struct { char _p5666[4236]; int unk979; };
        struct { char _p5667[4240]; int unk980; };
        struct { char _p5668[4244]; int unk981; };
        struct { char _p5669[4248]; int unk982; };
        struct { char _p5670[4252]; int unk983; };
        struct { char _p5671[4256]; int unk984; };
        struct { char _p5672[4260]; int unk985; };
        struct { char _p5673[4264]; int unk986; };
        struct { char _p5674[4268]; int unk987; };
        struct { char _p5675[4272]; int unk988; };
        struct { char _p5676[4276]; int unk989; };
        struct { char _p5677[4280]; int unk990; };
        struct { char _p5678[4284]; int unk991; };
        struct { char _p5679[4288]; int unk992; };
        struct { char _p5680[4292]; int unk993; };
        struct { char _p5681[4296]; int unk994; };
        struct { char _p5682[4300]; int unk995; };
        struct { char _p5683[4304]; int unk996; };
        struct { char _p5684[4308]; int unk997; };
        struct { char _p5685[4312]; int unk998; };
        struct { char _p5686[4320]; int unk1000; };
        struct { char _p5687[4324]; int unk1001; };
        struct { char _p5688[4328]; int unk1002; };
        struct { char _p5689[4332]; int unk1003; };
        struct { char _p5690[4336]; int unk1004; };
        struct { char _p5691[4340]; int unk1005; };
        struct { char _p5692[4344]; int unk1006; };
        struct { char _p5693[4348]; int unk1007; };
        struct { char _p5694[4352]; int unk1008; };
        struct { char _p5695[4356]; int unk1009; };
        struct { char _p5696[4360]; int unk1010; };
        struct { char _p5697[4364]; int unk1011; };
        struct { char _p5698[4440]; int unk1021; };
        struct { char _p5699[4448]; int unk1022; };
        struct { char _p5700[4452]; int unk1023; };
        struct { char _p5701[4456]; int unk1024; };
        struct { char _p5702[4460]; int unk1025; };
        struct { char _p5703[4464]; int unk1026; };
        struct { char _p5704[4468]; int unk1027; };
        struct { char _p5705[4472]; int unk1028; };
        struct { char _p5706[4476]; int unk1029; };
        struct { char _p5707[4480]; int unk1030; };
        struct { char _p5708[4484]; int unk1031; };
        struct { char _p5709[4488]; int unk1032; };
        struct { char _p5710[4492]; int unk1033; };
        struct { char _p5711[4496]; int unk1034; };
        struct { char _p5712[4500]; int unk1035; };
        struct { char _p5713[4504]; int unk1036; };
        struct { char _p5714[4508]; int unk1037; };
        struct { char _p5715[4512]; int unk1038; };
        struct { char _p5716[4516]; int unk1039; };
        struct { char _p5717[4520]; int unk1040; };
        struct { char _p5718[4524]; int unk1041; };
        struct { char _p5719[4528]; int unk1042; };
        struct { char _p5720[4532]; int unk1043; };
        struct { char _p5721[4536]; int unk1044; };
        struct { char _p5722[4540]; int unk1045; };
        struct { char _p5723[4544]; int unk1046; };
        struct { char _p5724[4548]; int unk1047; };
        struct { char _p5725[4552]; int unk1048; };
        struct { char _p5726[4556]; int unk1049; };
        struct { char _p5727[4560]; int unk1050; };
        struct { char _p5728[4564]; int unk1051; };
        struct { char _p5729[4568]; int unk1052; };
        struct { char _p5730[4572]; int unk1053; };
        struct { char _p5731[4576]; int unk1054; };
        struct { char _p5732[4580]; int unk1055; };
        struct { char _p5733[4584]; int unk1056; };
        struct { char _p5734[4588]; int unk1057; };
        struct { char _p5735[4592]; int unk1058; };
        struct { char _p5736[4596]; int unk1059; };
        struct { char _p5737[4600]; int unk1060; };
        struct { char _p5738[4604]; int unk1061; };
        struct { char _p5739[4608]; int unk1062; };
        struct { char _p5740[4612]; int unk1063; };
        struct { char _p5741[4616]; int unk1064; };
        struct { char _p5742[4620]; int unk1065; };
        struct { char _p5743[4624]; int unk1066; };
        struct { char _p5744[4628]; int unk1067; };
        struct { char _p5745[4632]; int unk1068; };
        struct { char _p5746[4636]; int unk1069; };
        struct { char _p5747[4640]; int unk1070; };
        struct { char _p5748[4644]; int unk1071; };
        struct { char _p5749[4648]; int unk1072; };
        struct { char _p5750[4652]; int unk1073; };
        struct { char _p5751[4656]; int unk1074; };
        struct { char _p5752[4660]; int unk1075; };
        struct { char _p5753[4664]; int unk1076; };
        struct { char _p5754[4668]; int unk1077; };
        struct { char _p5755[4672]; int unk1078; };
        struct { char _p5756[4680]; int unk1080; };
        struct { char _p5757[4684]; int unk1081; };
        struct { char _p5758[4688]; int unk1082; };
        struct { char _p5759[4692]; int unk1083; };
        struct { char _p5760[4696]; int unk1084; };
        struct { char _p5761[4700]; int unk1085; };
        struct { char _p5762[4704]; int unk1086; };
        struct { char _p5763[4708]; int unk1087; };
        struct { char _p5764[4712]; int unk1088; };
        struct { char _p5765[4716]; int unk1089; };
        struct { char _p5766[4720]; int unk1090; };
        struct { char _p5767[4724]; int unk1091; };
        struct { char _p5768[4800]; int unk1101; };
        struct { char _p5769[4808]; int unk1102; };
        struct { char _p5770[4812]; int unk1103; };
        struct { char _p5771[4816]; int unk1104; };
        struct { char _p5772[4820]; int unk1105; };
        struct { char _p5773[4824]; int unk1106; };
        struct { char _p5774[4828]; int unk1107; };
        struct { char _p5775[4832]; int unk1108; };
        struct { char _p5776[4836]; int unk1109; };
        struct { char _p5777[4840]; int unk1110; };
        struct { char _p5778[4844]; int unk1111; };
        struct { char _p5779[4848]; int unk1112; };
        struct { char _p5780[4852]; int unk1113; };
        struct { char _p5781[4856]; int unk1114; };
        struct { char _p5782[4860]; int unk1115; };
        struct { char _p5783[4864]; int unk1116; };
        struct { char _p5784[4868]; int unk1117; };
        struct { char _p5785[4872]; int unk1118; };
        struct { char _p5786[4876]; int unk1119; };
        struct { char _p5787[4880]; int unk1120; };
        struct { char _p5788[4884]; int unk1121; };
        struct { char _p5789[4888]; int unk1122; };
        struct { char _p5790[4892]; int unk1123; };
        struct { char _p5791[4896]; int unk1124; };
        struct { char _p5792[4900]; int unk1125; };
        struct { char _p5793[4904]; int unk1126; };
        struct { char _p5794[4908]; int unk1127; };
        struct { char _p5795[4912]; int unk1128; };
        struct { char _p5796[4916]; int unk1129; };
        struct { char _p5797[4920]; int unk1130; };
        struct { char _p5798[4924]; int unk1131; };
        struct { char _p5799[4928]; int unk1132; };
        struct { char _p5800[4932]; int unk1133; };
        struct { char _p5801[4936]; int unk1134; };
        struct { char _p5802[4940]; int unk1135; };
        struct { char _p5803[4944]; int unk1136; };
        struct { char _p5804[4948]; int unk1137; };
        struct { char _p5805[4952]; int unk1138; };
        struct { char _p5806[4956]; int unk1139; };
        struct { char _p5807[4960]; int unk1140; };
        struct { char _p5808[4964]; int unk1141; };
        struct { char _p5809[4968]; int unk1142; };
        struct { char _p5810[4972]; int unk1143; };
        struct { char _p5811[4976]; int unk1144; };
        struct { char _p5812[4980]; int unk1145; };
        struct { char _p5813[4984]; int unk1146; };
        struct { char _p5814[4988]; int unk1147; };
        struct { char _p5815[4992]; int unk1148; };
        struct { char _p5816[4996]; int unk1149; };
        struct { char _p5817[5000]; int unk1150; };
        struct { char _p5818[5004]; int unk1151; };
        struct { char _p5819[5008]; int unk1152; };
        struct { char _p5820[5012]; int unk1153; };
        struct { char _p5821[5016]; int unk1154; };
        struct { char _p5822[5020]; int unk1155; };
        struct { char _p5823[5024]; int unk1156; };
        struct { char _p5824[5028]; int unk1157; };
        struct { char _p5825[5032]; int unk1158; };
        struct { char _p5826[5040]; int unk1160; };
        struct { char _p5827[5044]; int unk1161; };
        struct { char _p5828[5048]; int unk1162; };
        struct { char _p5829[5052]; int unk1163; };
        struct { char _p5830[5056]; int unk1164; };
        struct { char _p5831[5060]; int unk1165; };
        struct { char _p5832[5064]; int unk1166; };
        struct { char _p5833[5068]; int unk1167; };
        struct { char _p5834[5072]; int unk1168; };
        struct { char _p5835[5076]; int unk1169; };
        struct { char _p5836[5080]; int unk1170; };
        struct { char _p5837[5084]; int unk1171; };
        struct { char _p5838[5160]; int unk1181; };
        struct { char _p5839[5168]; int unk1182; };
        struct { char _p5840[5172]; int unk1183; };
        struct { char _p5841[5176]; int unk1184; };
        struct { char _p5842[5180]; int unk1185; };
        struct { char _p5843[5184]; int unk1186; };
        struct { char _p5844[5188]; int unk1187; };
        struct { char _p5845[5192]; int unk1188; };
        struct { char _p5846[5196]; int unk1189; };
        struct { char _p5847[5200]; int unk1190; };
        struct { char _p5848[5204]; int unk1191; };
        struct { char _p5849[5208]; int unk1192; };
        struct { char _p5850[5212]; int unk1193; };
        struct { char _p5851[5216]; int unk1194; };
        struct { char _p5852[5220]; int unk1195; };
        struct { char _p5853[5224]; int unk1196; };
        struct { char _p5854[5228]; int unk1197; };
        struct { char _p5855[5232]; int unk1198; };
        struct { char _p5856[5236]; int unk1199; };
        struct { char _p5857[5240]; int unk1200; };
        struct { char _p5858[5244]; int unk1201; };
        struct { char _p5859[5248]; int unk1202; };
        struct { char _p5860[5252]; int unk1203; };
        struct { char _p5861[5256]; int unk1204; };
        struct { char _p5862[5260]; int unk1205; };
        struct { char _p5863[5264]; int unk1206; };
        struct { char _p5864[5268]; int unk1207; };
        struct { char _p5865[5272]; int unk1208; };
        struct { char _p5866[5276]; int unk1209; };
        struct { char _p5867[5280]; int unk1210; };
        struct { char _p5868[5284]; int unk1211; };
        struct { char _p5869[5288]; int unk1212; };
        struct { char _p5870[5292]; int unk1213; };
        struct { char _p5871[5296]; int unk1214; };
        struct { char _p5872[5300]; int unk1215; };
        struct { char _p5873[5304]; int unk1216; };
        struct { char _p5874[5308]; int unk1217; };
        struct { char _p5875[5312]; int unk1218; };
        struct { char _p5876[5316]; int unk1219; };
        struct { char _p5877[5320]; int unk1220; };
        struct { char _p5878[5324]; int unk1221; };
        struct { char _p5879[5328]; int unk1222; };
        struct { char _p5880[5332]; int unk1223; };
        struct { char _p5881[5336]; int unk1224; };
        struct { char _p5882[5340]; int unk1225; };
        struct { char _p5883[5344]; int unk1226; };
        struct { char _p5884[5348]; int unk1227; };
        struct { char _p5885[5352]; int unk1228; };
        struct { char _p5886[5356]; int unk1229; };
        struct { char _p5887[5360]; int unk1230; };
        struct { char _p5888[5364]; int unk1231; };
        struct { char _p5889[5368]; int unk1232; };
        struct { char _p5890[5372]; int unk1233; };
        struct { char _p5891[5376]; int unk1234; };
        struct { char _p5892[5380]; int unk1235; };
        struct { char _p5893[5384]; int unk1236; };
        struct { char _p5894[5388]; int unk1237; };
        struct { char _p5895[5392]; int unk1238; };
        struct { char _p5896[5400]; int unk1240; };
        struct { char _p5897[5404]; int unk1241; };
        struct { char _p5898[5408]; int unk1242; };
        struct { char _p5899[5412]; int unk1243; };
        struct { char _p5900[5416]; int unk1244; };
        struct { char _p5901[5420]; int unk1245; };
        struct { char _p5902[5424]; int unk1246; };
        struct { char _p5903[5428]; int unk1247; };
        struct { char _p5904[5432]; int unk1248; };
        struct { char _p5905[5436]; int unk1249; };
        struct { char _p5906[5440]; int unk1250; };
        struct { char _p5907[5444]; int unk1251; };
        struct { char _p5908[5520]; int unk1261; };
        struct { char _p5909[5528]; int unk1262; };
        struct { char _p5910[5532]; int unk1263; };
        struct { char _p5911[5536]; int unk1264; };
        struct { char _p5912[5540]; int unk1265; };
        struct { char _p5913[5544]; int unk1266; };
        struct { char _p5914[5548]; int unk1267; };
        struct { char _p5915[5552]; int unk1268; };
        struct { char _p5916[5556]; int unk1269; };
        struct { char _p5917[5560]; int unk1270; };
        struct { char _p5918[5564]; int unk1271; };
        struct { char _p5919[5568]; int unk1272; };
        struct { char _p5920[5572]; int unk1273; };
        struct { char _p5921[5576]; int unk1274; };
        struct { char _p5922[5580]; int unk1275; };
        struct { char _p5923[5584]; int unk1276; };
        struct { char _p5924[5588]; int unk1277; };
        struct { char _p5925[5592]; int unk1278; };
        struct { char _p5926[5596]; int unk1279; };
        struct { char _p5927[5600]; int unk1280; };
        struct { char _p5928[5604]; int unk1281; };
        struct { char _p5929[5608]; int unk1282; };
        struct { char _p5930[5612]; int unk1283; };
        struct { char _p5931[5616]; int unk1284; };
        struct { char _p5932[5620]; int unk1285; };
        struct { char _p5933[5624]; int unk1286; };
        struct { char _p5934[5628]; int unk1287; };
        struct { char _p5935[5632]; int unk1288; };
        struct { char _p5936[5636]; int unk1289; };
        struct { char _p5937[5640]; int unk1290; };
        struct { char _p5938[5644]; int unk1291; };
        struct { char _p5939[5648]; int unk1292; };
        struct { char _p5940[5652]; int unk1293; };
        struct { char _p5941[5656]; int unk1294; };
        struct { char _p5942[5660]; int unk1295; };
        struct { char _p5943[5664]; int unk1296; };
        struct { char _p5944[5668]; int unk1297; };
        struct { char _p5945[5672]; int unk1298; };
        struct { char _p5946[5676]; int unk1299; };
        struct { char _p5947[5680]; int unk1300; };
        struct { char _p5948[5684]; int unk1301; };
        struct { char _p5949[5688]; int unk1302; };
        struct { char _p5950[5692]; int unk1303; };
        struct { char _p5951[5696]; int unk1304; };
        struct { char _p5952[5700]; int unk1305; };
        struct { char _p5953[5704]; int unk1306; };
        struct { char _p5954[5708]; int unk1307; };
        struct { char _p5955[5712]; int unk1308; };
        struct { char _p5956[5716]; int unk1309; };
        struct { char _p5957[5720]; int unk1310; };
        struct { char _p5958[5724]; int unk1311; };
        struct { char _p5959[5728]; int unk1312; };
        struct { char _p5960[5732]; int unk1313; };
        struct { char _p5961[5736]; int unk1314; };
        struct { char _p5962[5740]; int unk1315; };
        struct { char _p5963[5744]; int unk1316; };
        struct { char _p5964[5748]; int unk1317; };
        struct { char _p5965[5752]; int unk1318; };
        struct { char _p5966[5760]; int unk1320; };
        struct { char _p5967[5764]; int unk1321; };
        struct { char _p5968[5768]; int unk1322; };
        struct { char _p5969[5772]; int unk1323; };
        struct { char _p5970[5776]; int unk1324; };
        struct { char _p5971[5780]; int unk1325; };
        struct { char _p5972[5784]; int unk1326; };
        struct { char _p5973[5788]; int unk1327; };
        struct { char _p5974[5792]; int unk1328; };
        struct { char _p5975[5796]; int unk1329; };
        struct { char _p5976[5800]; int unk1330; };
        struct { char _p5977[5804]; int unk1331; };
        struct { char _p5978[5880]; int unk1341; };
        struct { char _p5979[5888]; int unk1342; };
        struct { char _p5980[5892]; int unk1343; };
        struct { char _p5981[5896]; int unk1344; };
        struct { char _p5982[5900]; int unk1345; };
        struct { char _p5983[5904]; int unk1346; };
        struct { char _p5984[5908]; int unk1347; };
        struct { char _p5985[5912]; int unk1348; };
        struct { char _p5986[5916]; int unk1349; };
        struct { char _p5987[5920]; int unk1350; };
        struct { char _p5988[5924]; int unk1351; };
        struct { char _p5989[5928]; int unk1352; };
        struct { char _p5990[5932]; int unk1353; };
        struct { char _p5991[5936]; int unk1354; };
        struct { char _p5992[5940]; int unk1355; };
        struct { char _p5993[5944]; int unk1356; };
        struct { char _p5994[5948]; int unk1357; };
        struct { char _p5995[5952]; int unk1358; };
        struct { char _p5996[5956]; int unk1359; };
        struct { char _p5997[5960]; int unk1360; };
        struct { char _p5998[5964]; int unk1361; };
        struct { char _p5999[5968]; int unk1362; };
        struct { char _p6000[5972]; int unk1363; };
        struct { char _p6001[5976]; int unk1364; };
        struct { char _p6002[5980]; int unk1365; };
        struct { char _p6003[5984]; int unk1366; };
        struct { char _p6004[5988]; int unk1367; };
        struct { char _p6005[5992]; int unk1368; };
        struct { char _p6006[5996]; int unk1369; };
        struct { char _p6007[6000]; int unk1370; };
        struct { char _p6008[6004]; int unk1371; };
        struct { char _p6009[6008]; int unk1372; };
        struct { char _p6010[6012]; int unk1373; };
        struct { char _p6011[6016]; int unk1374; };
        struct { char _p6012[6020]; int unk1375; };
        struct { char _p6013[6024]; int unk1376; };
        struct { char _p6014[6028]; int unk1377; };
        struct { char _p6015[6032]; int unk1378; };
        struct { char _p6016[6036]; int unk1379; };
        struct { char _p6017[6040]; int unk1380; };
        struct { char _p6018[6044]; int unk1381; };
        struct { char _p6019[6048]; int unk1382; };
        struct { char _p6020[6052]; int unk1383; };
        struct { char _p6021[6056]; int unk1384; };
        struct { char _p6022[6060]; int unk1385; };
        struct { char _p6023[6064]; int unk1386; };
        struct { char _p6024[6068]; int unk1387; };
        struct { char _p6025[6072]; int unk1388; };
        struct { char _p6026[6076]; int unk1389; };
        struct { char _p6027[6080]; int unk1390; };
        struct { char _p6028[6084]; int unk1391; };
        struct { char _p6029[6088]; int unk1392; };
        struct { char _p6030[6092]; int unk1393; };
        struct { char _p6031[6096]; int unk1394; };
        struct { char _p6032[6100]; int unk1395; };
        struct { char _p6033[6104]; int unk1396; };
        struct { char _p6034[6108]; int unk1397; };
        struct { char _p6035[6112]; int unk1398; };
        struct { char _p6036[6120]; int unk1400; };
        struct { char _p6037[6124]; int unk1401; };
        struct { char _p6038[6128]; int unk1402; };
        struct { char _p6039[6132]; int unk1403; };
        struct { char _p6040[6136]; int unk1404; };
        struct { char _p6041[6140]; int unk1405; };
        struct { char _p6042[6144]; int unk1406; };
        struct { char _p6043[6148]; int unk1407; };
        struct { char _p6044[6152]; int unk1408; };
        struct { char _p6045[6156]; int unk1409; };
        struct { char _p6046[6160]; int unk1410; };
        struct { char _p6047[6164]; int unk1411; };
        struct { char _p6048[6240]; int unk1421; };
        struct { char _p6049[6248]; int unk1422; };
        struct { char _p6050[6252]; int unk1423; };
        struct { char _p6051[6256]; int unk1424; };
        struct { char _p6052[6260]; int unk1425; };
        struct { char _p6053[6264]; int unk1426; };
        struct { char _p6054[6268]; int unk1427; };
        struct { char _p6055[6272]; int unk1428; };
        struct { char _p6056[6276]; int unk1429; };
        struct { char _p6057[6280]; int unk1430; };
        struct { char _p6058[6284]; int unk1431; };
        struct { char _p6059[6288]; int unk1432; };
        struct { char _p6060[6292]; int unk1433; };
        struct { char _p6061[6296]; int unk1434; };
        struct { char _p6062[6300]; int unk1435; };
        struct { char _p6063[6304]; int unk1436; };
        struct { char _p6064[6308]; int unk1437; };
        struct { char _p6065[6312]; int unk1438; };
        struct { char _p6066[6316]; int unk1439; };
        struct { char _p6067[6320]; int unk1440; };
        struct { char _p6068[6324]; int unk1441; };
        struct { char _p6069[6328]; int unk1442; };
        struct { char _p6070[6332]; int unk1443; };
        struct { char _p6071[6336]; int unk1444; };
        struct { char _p6072[6340]; int unk1445; };
        struct { char _p6073[6344]; int unk1446; };
        struct { char _p6074[6348]; int unk1447; };
        struct { char _p6075[6352]; int unk1448; };
        struct { char _p6076[6356]; int unk1449; };
        struct { char _p6077[6360]; int unk1450; };
        struct { char _p6078[6364]; int unk1451; };
        struct { char _p6079[6368]; int unk1452; };
        struct { char _p6080[6372]; int unk1453; };
        struct { char _p6081[6376]; int unk1454; };
        struct { char _p6082[6380]; int unk1455; };
        struct { char _p6083[6384]; int unk1456; };
        struct { char _p6084[6388]; int unk1457; };
        struct { char _p6085[6392]; int unk1458; };
        struct { char _p6086[6396]; int unk1459; };
        struct { char _p6087[6400]; int unk1460; };
        struct { char _p6088[6404]; int unk1461; };
        struct { char _p6089[6408]; int unk1462; };
        struct { char _p6090[6412]; int unk1463; };
        struct { char _p6091[6416]; int unk1464; };
        struct { char _p6092[6420]; int unk1465; };
        struct { char _p6093[6424]; int unk1466; };
        struct { char _p6094[6428]; int unk1467; };
        struct { char _p6095[6432]; int unk1468; };
        struct { char _p6096[6436]; int unk1469; };
        struct { char _p6097[6440]; int unk1470; };
        struct { char _p6098[6444]; int unk1471; };
        struct { char _p6099[6448]; int unk1472; };
        struct { char _p6100[6452]; int unk1473; };
        struct { char _p6101[6456]; int unk1474; };
        struct { char _p6102[6460]; int unk1475; };
        struct { char _p6103[6464]; int unk1476; };
        struct { char _p6104[6468]; int unk1477; };
        struct { char _p6105[6472]; int unk1478; };
        struct { char _p6106[6480]; int unk1480; };
        struct { char _p6107[6484]; int unk1481; };
        struct { char _p6108[6488]; int unk1482; };
        struct { char _p6109[6492]; int unk1483; };
        struct { char _p6110[6496]; int unk1484; };
        struct { char _p6111[6500]; int unk1485; };
        struct { char _p6112[6504]; int unk1486; };
        struct { char _p6113[6508]; int unk1487; };
        struct { char _p6114[6512]; int unk1488; };
        struct { char _p6115[6516]; int unk1489; };
        struct { char _p6116[6520]; int unk1490; };
        struct { char _p6117[6524]; int unk1491; };
        struct { char _p6118[6600]; int unk1501; };
        struct { char _p6119[6608]; int unk1502; };
        struct { char _p6120[6612]; int unk1503; };
        struct { char _p6121[6616]; int unk1504; };
        struct { char _p6122[6620]; int unk1505; };
        struct { char _p6123[6624]; int unk1506; };
        struct { char _p6124[6628]; int unk1507; };
        struct { char _p6125[6632]; int unk1508; };
        struct { char _p6126[6636]; int unk1509; };
        struct { char _p6127[6640]; int unk1510; };
        struct { char _p6128[6644]; int unk1511; };
        struct { char _p6129[6648]; int unk1512; };
        struct { char _p6130[6652]; int unk1513; };
        struct { char _p6131[6656]; int unk1514; };
        struct { char _p6132[6660]; int unk1515; };
        struct { char _p6133[6664]; int unk1516; };
        struct { char _p6134[6668]; int unk1517; };
        struct { char _p6135[6672]; int unk1518; };
        struct { char _p6136[6676]; int unk1519; };
        struct { char _p6137[6680]; int unk1520; };
        struct { char _p6138[6684]; int unk1521; };
        struct { char _p6139[6688]; int unk1522; };
        struct { char _p6140[6692]; int unk1523; };
        struct { char _p6141[6696]; int unk1524; };
        struct { char _p6142[6700]; int unk1525; };
        struct { char _p6143[6704]; int unk1526; };
        struct { char _p6144[6708]; int unk1527; };
        struct { char _p6145[6712]; int unk1528; };
        struct { char _p6146[6716]; int unk1529; };
        struct { char _p6147[6720]; int unk1530; };
        struct { char _p6148[6724]; int unk1531; };
        struct { char _p6149[6728]; int unk1532; };
        struct { char _p6150[6732]; int unk1533; };
        struct { char _p6151[6736]; int unk1534; };
        struct { char _p6152[6740]; int unk1535; };
        struct { char _p6153[6744]; int unk1536; };
        struct { char _p6154[6748]; int unk1537; };
        struct { char _p6155[6752]; int unk1538; };
        struct { char _p6156[6756]; int unk1539; };
        struct { char _p6157[6760]; int unk1540; };
        struct { char _p6158[6764]; int unk1541; };
        struct { char _p6159[6768]; int unk1542; };
        struct { char _p6160[6772]; int unk1543; };
        struct { char _p6161[6776]; int unk1544; };
        struct { char _p6162[6780]; int unk1545; };
        struct { char _p6163[6784]; int unk1546; };
        struct { char _p6164[6788]; int unk1547; };
        struct { char _p6165[6792]; int unk1548; };
        struct { char _p6166[6796]; int unk1549; };
        struct { char _p6167[6800]; int unk1550; };
        struct { char _p6168[6804]; int unk1551; };
        struct { char _p6169[6808]; int unk1552; };
        struct { char _p6170[6812]; int unk1553; };
        struct { char _p6171[6816]; int unk1554; };
        struct { char _p6172[6820]; int unk1555; };
        struct { char _p6173[6824]; u8 x1850; };
        struct { char _p6174[6825]; u8 menu_bgm; };
        struct { char _p6175[6826]; u8 x1852; };
        struct { char _p6176[6827]; u8 x1853; };
        struct { char _p6177[6828]; int unk1557; };
        struct { char _p6178[6832]; int unk1558; };
        struct { char _p6179[6836]; int unk1559; };
        struct { char _p6180[6840]; int unk1560; };
        struct { char _p6181[6844]; int unk1561; };
        struct { char _p6182[6848]; int unk1562; };
        struct { char _p6183[6852]; int unk1563; };
        struct { char _p6184[6856]; int unk1564; };
        struct { char _p6185[6860]; int unk1565; };
        struct { char _p6186[6864]; int unk1566; };
        struct { char _p6187[6868]; int unk1567; };
        struct { char _p6188[6872]; int unk1568; };
        struct { char _p6189[6876]; int unk1569; };
        struct { char _p6190[6880]; int unk1570; };
        struct { char _p6191[6884]; int unk1571; };
        struct { char _p6192[6888]; int unk1572; };
        struct { char _p6193[6892]; int unk1573; };
        struct { char _p6194[6896]; int unk1574; };
        struct { char _p6195[6900]; int unk1575; };
        struct { char _p6196[6904]; int unk1576; };
        struct { char _p6197[6908]; int unk1577; };
        struct { char _p6198[6912]; int unk1578; };
        struct { char _p6199[6916]; int unk1579; };
        struct { char _p6200[6920]; int unk1580; };
        struct { char _p6201[6924]; int unk1581; };
        struct { char _p6202[6928]; int unk1582; };
        struct { char _p6203[6932]; int unk1583; };
        struct { char _p6204[6936]; int unk1584; };
        struct { char _p6205[6940]; int unk1585; };
        struct { char _p6206[6944]; int unk1586; };
        struct { char _p6207[6948]; int unk1587; };
        struct { char _p6208[6952]; int unk1588; };
        struct { char _p6209[6956]; int unk1589; };
        struct { char _p6210[6960]; int unk1590; };
        struct { char _p6211[6964]; int unk1591; };
        struct { char _p6212[6968]; int unk1592; };
        struct { char _p6213[6972]; int unk1593; };
        struct { char _p6214[6976]; int unk1594; };
        struct { char _p6215[6980]; int unk1595; };
        struct { char _p6216[6984]; int unk1596; };
        struct { char _p6217[6988]; int unk1597; };
        struct { char _p6218[6992]; int unk1598; };
        struct { char _p6219[6996]; int unk1599; };
        struct { char _p6220[7000]; int unk1600; };
        struct { char _p6221[7004]; int unk1601; };
        struct { char _p6222[7008]; int unk1602; };
        struct { char _p6223[7012]; int unk1603; };
        struct { char _p6224[7016]; int unk1604; };
        struct { char _p6225[7020]; int unk1605; };
        struct { char _p6226[7024]; int unk1606; };
        struct { char _p6227[7028]; int unk1607; };
        struct { char _p6228[7032]; int unk1608; };
        struct { char _p6229[7036]; int unk1609; };
        struct { char _p6230[7040]; int unk1610; };
        struct { char _p6231[7044]; int unk1611; };
        struct { char _p6232[7048]; int unk1612; };
        struct { char _p6233[7052]; int unk1613; };
        struct { char _p6234[7056]; int unk1614; };
        struct { char _p6235[7060]; int unk1615; };
        struct { char _p6236[7064]; int unk1616; };
        struct { char _p6237[7068]; int unk1617; };
        struct { char _p6238[7072]; int unk1618; };
        struct { char _p6239[7076]; int unk1619; };
        struct { char _p6240[7080]; int unk1620; };
        struct { char _p6241[7084]; int unk1621; };
        struct { char _p6242[7088]; int unk1622; };
        struct { char _p6243[7092]; int unk1623; };
        struct { char _p6244[7096]; int unk1624; };
        struct { char _p6245[7100]; int unk1625; };
        struct { char _p6246[7104]; int unk1626; };
        struct { char _p6247[7108]; int unk1627; };
        struct { char _p6248[7112]; int unk1628; };
        struct { char _p6249[7116]; int unk1629; };
        struct { char _p6250[7120]; int unk1630; };
        struct { char _p6251[7124]; int unk1631; };
        struct { char _p6252[7128]; int unk1632; };
        struct { char _p6253[7132]; int unk1633; };
        struct { char _p6254[7136]; int unk1634; };
        struct { char _p6255[7140]; int unk1635; };
        struct { char _p6256[7144]; int unk1636; };
        struct { char _p6257[7148]; int unk1637; };
        struct { char _p6258[7152]; int unk1638; };
        struct { char _p6259[7156]; int unk1639; };
        struct { char _p6260[7160]; int unk1640; };
        struct { char _p6261[7164]; int unk1641; };
        struct { char _p6262[7168]; int unk1642; };
        struct { char _p6263[7172]; int unk1643; };
        struct { char _p6264[7176]; int unk1644; };
        struct { char _p6265[7180]; int unk1645; };
        struct { char _p6266[7184]; int unk1646; };
        struct { char _p6267[7188]; int unk1647; };
        struct { char _p6268[7192]; int unk1648; };
        struct { char _p6269[7196]; int unk1649; };
        struct { char _p6270[7200]; int unk1650; };
        struct { char _p6271[7204]; int unk1651; };
        struct { char _p6272[7208]; int unk1652; };
        struct { char _p6273[7212]; int unk1653; };
        struct { char _p6274[7216]; int unk1654; };
        struct { char _p6275[7220]; int unk1655; };
        struct { char _p6276[7224]; int unk1656; };
        struct { char _p6277[7228]; int unk1657; };
        struct { char _p6278[7232]; int unk1658; };
        struct { char _p6279[7236]; int unk1659; };
        struct { char _p6280[7240]; int unk1660; };
        struct { char _p6281[7244]; int unk1661; };
        struct { char _p6282[7248]; int unk1662; };
        struct { char _p6283[7252]; int unk1663; };
        struct { char _p6284[7256]; int unk1664; };
        struct { char _p6285[7260]; int unk1665; };
        struct { char _p6286[7264]; int unk1666; };
        struct { char _p6287[7268]; int unk1667; };
        struct { char _p6288[7272]; int unk1668; };
        struct { char _p6289[7276]; int unk1669; };
        struct { char _p6290[7280]; int unk1670; };
        struct { char _p6291[7284]; int unk1671; };
        struct { char _p6292[7288]; int unk1672; };
        struct { char _p6293[7292]; int unk1673; };
        struct { char _p6294[7296]; int unk1674; };
        struct { char _p6295[7300]; int unk1675; };
        struct { char _p6296[7304]; int unk1676; };
        struct { char _p6297[7308]; int unk1677; };
        struct { char _p6298[7312]; int unk1678; };
        struct { char _p6299[7316]; int unk1679; };
        struct { char _p6300[7320]; int unk1680; };
        struct { char _p6301[7324]; int unk1681; };
        struct { char _p6302[7328]; int unk1682; };
        struct { char _p6303[7332]; int unk1683; };
        struct { char _p6304[7336]; int unk1684; };
        struct { char _p6305[7340]; int unk1685; };
        struct { char _p6306[7344]; int unk1686; };
        struct { char _p6307[7348]; int unk1687; };
        struct { char _p6308[7352]; int unk1688; };
        struct { char _p6309[7356]; int unk1689; };
        struct { char _p6310[7360]; int unk1690; };
        struct { char _p6311[7364]; int unk1691; };
        struct { char _p6312[7368]; int unk1692; };
        struct { char _p6313[7372]; int unk1693; };
        struct { char _p6314[7376]; int unk1694; };
        struct { char _p6315[7380]; int unk1695; };
        struct { char _p6316[7384]; int unk1696; };
        struct { char _p6317[7388]; int unk1697; };
        struct { char _p6318[7392]; int unk1698; };
        struct { char _p6319[7396]; int unk1699; };
        struct { char _p6320[7400]; int unk1700; };
        struct { char _p6321[7404]; int unk1701; };
        struct { char _p6322[7408]; int unk1702; };
        struct { char _p6323[7412]; int unk1703; };
        struct { char _p6324[7416]; int unk1704; };
        struct { char _p6325[7420]; int unk1705; };
        struct { char _p6326[7424]; int unk1706; };
        struct { char _p6327[7428]; int unk1707; };
        struct { char _p6328[7432]; int unk1708; };
        struct { char _p6329[7436]; int unk1709; };
        struct { char _p6330[7440]; int unk1710; };
        struct { char _p6331[7444]; int unk1711; };
        struct { char _p6332[7448]; int unk1712; };
        struct { char _p6333[7452]; int unk1713; };
        struct { char _p6334[7456]; int unk1714; };
        struct { char _p6335[7460]; int unk1715; };
        struct { char _p6336[7464]; int unk1716; };
        struct { char _p6337[7468]; int unk1717; };
        struct { char _p6338[7472]; int unk1718; };
        struct { char _p6339[7476]; int unk1719; };
        struct { char _p6340[7480]; int unk1720; };
        struct { char _p6341[7484]; int unk1721; };
        struct { char _p6342[7488]; int unk1722; };
        struct { char _p6343[7492]; int unk1723; };
        struct { char _p6344[7496]; int unk1724; };
        struct { char _p6345[7500]; int unk1725; };
        struct { char _p6346[7504]; int unk1726; };
        struct { char _p6347[7508]; int unk1727; };
        struct { char _p6348[7512]; int unk1728; };
        struct { char _p6349[7516]; int unk1729; };
        struct { char _p6350[7520]; int unk1730; };
        struct { char _p6351[7524]; int unk1731; };
        struct { char _p6352[7528]; int unk1732; };
        struct { char _p6353[7532]; int unk1733; };
        struct { char _p6354[7536]; int unk1734; };
        struct { char _p6355[7540]; int unk1735; };
        struct { char _p6356[7544]; int unk1736; };
        struct { char _p6357[7548]; int unk1737; };
        struct { char _p6358[7552]; int unk1738; };
        struct { char _p6359[7556]; int unk1739; };
        struct { char _p6360[7560]; int unk1740; };
        struct { char _p6361[7564]; int unk1741; };
        struct { char _p6362[7568]; int unk1742; };
        struct { char _p6363[7572]; int unk1743; };
        struct { char _p6364[7576]; int unk1744; };
        struct { char _p6365[7580]; int unk1745; };
        struct { char _p6366[7584]; int unk1746; };
        struct { char _p6367[7588]; int unk1747; };
        struct { char _p6368[7592]; int unk1748; };
        struct { char _p6369[7596]; int unk1749; };
        struct { char _p6370[7600]; int unk1750; };
        struct { char _p6371[7604]; int unk1751; };
        struct { char _p6372[7608]; int unk1752; };
        struct { char _p6373[7612]; int unk1753; };
        struct { char _p6374[7616]; int unk1754; };
        struct { char _p6375[7620]; int unk1755; };
        struct { char _p6376[7624]; int unk1756; };
        struct { char _p6377[7628]; int unk1757; };
        struct { char _p6378[7632]; int unk1758; };
        struct { char _p6379[7636]; int unk1759; };
        struct { char _p6380[7640]; int unk1760; };
        struct { char _p6381[7644]; int unk1761; };
        struct { char _p6382[7648]; int unk1762; };
        struct { char _p6383[7652]; int unk1763; };
        struct { char _p6384[7656]; int unk1764; };
        struct { char _p6385[7660]; int unk1765; };
        struct { char _p6386[7664]; int unk1766; };
        struct { char _p6387[7668]; int unk1767; };
        struct { char _p6388[7672]; int unk1768; };
        struct { char _p6389[7676]; int unk1769; };
        struct { char _p6390[7680]; int unk1770; };
        struct { char _p6391[7684]; int unk1771; };
        struct { char _p6392[7688]; int unk1772; };
        struct { char _p6393[7692]; int unk1773; };
        struct { char _p6394[7696]; int unk1774; };
        struct { char _p6395[7700]; int unk1775; };
        struct { char _p6396[7704]; int unk1776; };
        struct { char _p6397[7708]; int unk1777; };
        struct { char _p6398[7712]; int unk1778; };
        struct { char _p6399[7716]; int unk1779; };
        struct { char _p6400[7720]; int unk1780; };
        struct { char _p6401[7724]; int unk1781; };
        struct { char _p6402[7728]; int unk1782; };
        struct { char _p6403[7732]; int unk1783; };
        struct { char _p6404[7736]; int unk1784; };
        struct { char _p6405[7740]; int unk1785; };
        struct { char _p6406[7744]; int unk1786; };
        struct { char _p6407[7748]; int unk1787; };
        struct { char _p6408[7752]; int unk1788; };
        struct { char _p6409[7756]; int unk1789; };
        struct { char _p6410[7760]; int unk1790; };
        struct { char _p6411[7764]; int unk1791; };
        struct { char _p6412[7768]; int unk1792; };
        struct { char _p6413[7772]; int unk1793; };
        struct { char _p6414[7776]; int unk1794; };
        struct { char _p6415[7780]; int unk1795; };
        struct { char _p6416[7784]; int unk1796; };
        struct { char _p6417[7788]; int unk1797; };
        struct { char _p6418[7792]; int unk1798; };
        struct { char _p6419[7796]; int unk1799; };
        struct { char _p6420[7800]; int unk1800; };
        struct { char _p6421[7804]; int unk1801; };
        struct { char _p6422[7808]; int unk1802; };
        struct { char _p6423[7812]; int unk1803; };
        struct { char _p6424[7816]; int unk1804; };
        struct { char _p6425[7820]; int unk1805; };
        struct { char _p6426[7824]; int unk1806; };
        struct { char _p6427[7828]; int unk1807; };
        struct { char _p6428[7832]; int unk1808; };
        struct { char _p6429[7836]; int unk1809; };
        struct { char _p6430[7840]; int unk1810; };
        struct { char _p6431[7844]; int unk1811; };
        struct { char _p6432[7848]; int unk1812; };
        struct { char _p6433[7852]; int unk1813; };
        struct { char _p6434[7856]; int unk1814; };
        struct { char _p6435[7860]; int unk1815; };
        struct { char _p6436[7864]; int unk1816; };
        struct { char _p6437[7868]; int unk1817; };
        struct { char _p6438[7872]; int unk1818; };
        struct { char _p6439[7876]; int unk1819; };
        struct { char _p6440[7880]; int unk1820; };
        struct { char _p6441[7884]; int unk1821; };
        struct { char _p6442[7888]; int unk1822; };
        struct { char _p6443[7892]; int unk1823; };
        struct { char _p6444[7896]; int unk1824; };
        struct { char _p6445[7900]; int unk1825; };
        struct { char _p6446[7904]; int unk1826; };
        struct { char _p6447[7908]; int unk1827; };
        struct { char _p6448[7912]; int unk1828; };
        struct { char _p6449[7916]; int unk1829; };
        struct { char _p6450[7920]; int unk1830; };
        struct { char _p6451[7924]; int unk1831; };
        struct { char _p6452[7928]; int unk1832; };
        struct { char _p6453[7932]; int unk1833; };
        struct { char _p6454[7936]; int unk1834; };
        struct { char _p6455[7940]; int unk1835; };
        struct { char _p6456[7944]; int unk1836; };
        struct { char _p6457[7948]; int unk1837; };
        struct { char _p6458[7952]; int unk1838; };
        struct { char _p6459[7956]; int unk1839; };
        struct { char _p6460[7960]; int unk1840; };
        struct { char _p6461[7964]; int unk1841; };
        struct { char _p6462[7968]; int unk1842; };
        struct { char _p6463[7972]; int unk1843; };
        struct { char _p6464[7976]; int unk1844; };
        struct { char _p6465[7980]; int unk1845; };
        struct { char _p6466[7984]; int unk1846; };
        struct { char _p6467[7988]; int unk1847; };
        struct { char _p6468[7992]; int unk1848; };
        struct { char _p6469[7996]; int unk1849; };
        struct { char _p6470[8000]; int unk1850; };
        struct { char _p6471[8004]; int unk1851; };
        struct { char _p6472[8008]; int unk1852; };
        struct { char _p6473[8012]; int unk1853; };
        struct { char _p6474[8016]; int unk1854; };
        struct { char _p6475[8020]; int unk1855; };
        struct { char _p6476[8024]; int unk1856; };
        struct { char _p6477[8028]; int unk1857; };
        struct { char _p6478[8032]; int unk1858; };
        struct { char _p6479[8036]; int unk1859; };
        struct { char _p6480[8040]; int unk1860; };
        struct { char _p6481[8044]; int unk1861; };
        struct { char _p6482[8048]; int unk1862; };
        struct { char _p6483[8052]; int unk1863; };
        struct { char _p6484[8056]; int unk1864; };
        struct { char _p6485[8060]; int unk1865; };
        struct { char _p6486[8064]; int unk1866; };
        struct { char _p6487[8068]; int unk1867; };
        struct { char _p6488[8072]; int unk1868; };
        struct { char _p6489[8076]; int unk1869; };
        struct { char _p6490[8080]; int unk1870; };
        struct { char _p6491[8084]; int unk1871; };
        struct { char _p6492[8088]; int unk1872; };
        struct { char _p6493[8092]; int unk1873; };
        struct { char _p6494[8096]; int unk1874; };
        struct { char _p6495[8100]; int unk1875; };
        struct { char _p6496[8104]; int unk1876; };
        struct { char _p6497[8108]; int unk1877; };
        struct { char _p6498[8112]; int unk1878; };
        struct { char _p6499[8116]; int unk1879; };
        struct { char _p6500[8120]; int unk1880; };
        struct { char _p6501[8124]; int unk1881; };
        struct { char _p6502[8128]; int unk1882; };
        struct { char _p6503[8132]; int unk1883; };
        struct { char _p6504[8136]; int unk1884; };
        struct { char _p6505[8140]; int unk1885; };
        struct { char _p6506[8144]; int unk1886; };
        struct { char _p6507[8148]; int unk1887; };
        struct { char _p6508[8152]; int unk1888; };
        struct { char _p6509[8156]; int unk1889; };
        struct { char _p6510[8160]; int unk1890; };
        struct { char _p6511[8164]; int unk1891; };
        struct { char _p6512[8168]; int unk1892; };
        struct { char _p6513[8172]; int unk1893; };
        struct { char _p6514[8176]; int unk1894; };
        struct { char _p6515[8180]; int unk1895; };
        struct { char _p6516[8184]; int unk1896; };
        struct { char _p6517[8188]; int unk1897; };
        struct { char _p6518[8192]; int unk1898; };
        struct { char _p6519[8196]; int unk1899; };
        struct { char _p6520[8200]; int unk1900; };
        struct { char _p6521[8204]; int unk1901; };
        struct { char _p6522[8208]; int unk1902; };
        struct { char _p6523[8212]; int unk1903; };
        struct { char _p6524[8216]; int unk1904; };
        struct { char _p6525[8220]; int unk1905; };
        struct { char _p6526[8224]; int unk1906; };
        struct { char _p6527[8228]; int unk1907; };
        struct { char _p6528[8232]; int unk1908; };
        struct { char _p6529[8236]; int unk1909; };
        struct { char _p6530[8240]; int unk1910; };
        struct { char _p6531[8244]; int unk1911; };
        struct { char _p6532[8248]; int unk1912; };
        struct { char _p6533[8252]; int unk1913; };
        struct { char _p6534[8256]; int unk1914; };
        struct { char _p6535[8260]; int unk1915; };
        struct { char _p6536[8264]; int unk1916; };
        struct { char _p6537[8268]; int unk1917; };
        struct { char _p6538[8272]; int unk1918; };
        struct { char _p6539[8276]; int unk1919; };
        struct { char _p6540[8280]; int unk1920; };
        struct { char _p6541[8284]; int unk1921; };
        struct { char _p6542[8288]; int unk1922; };
        struct { char _p6543[8292]; int unk1923; };
        struct { char _p6544[8296]; int unk1924; };
        struct { char _p6545[8300]; int unk1925; };
        struct { char _p6546[8304]; int unk1926; };
        struct { char _p6547[8308]; int unk1927; };
        struct { char _p6548[8312]; int unk1928; };
        struct { char _p6549[8316]; int unk1929; };
        struct { char _p6550[8320]; int unk1930; };
        struct { char _p6551[8324]; int unk1931; };
        struct { char _p6552[8328]; int unk1932; };
        struct { char _p6553[8332]; int unk1933; };
        struct { char _p6554[8336]; int unk1934; };
        struct { char _p6555[8340]; int unk1935; };
        struct { char _p6556[8344]; int unk1936; };
        struct { char _p6557[8348]; int unk1937; };
        struct { char _p6558[8352]; int unk1938; };
        struct { char _p6559[8356]; int unk1939; };
        struct { char _p6560[8360]; int unk1940; };
        struct { char _p6561[8364]; int unk1941; };
        struct { char _p6562[8368]; int unk1942; };
        struct { char _p6563[8372]; int unk1943; };
        struct { char _p6564[8376]; int unk1944; };
        struct { char _p6565[8380]; int unk1945; };
        struct { char _p6566[8384]; int unk1946; };
        struct { char _p6567[8388]; int unk1947; };
        struct { char _p6568[8392]; int unk1948; };
        struct { char _p6569[8396]; int unk1949; };
        struct { char _p6570[8400]; int unk1950; };
        struct { char _p6571[8404]; int unk1951; };
        struct { char _p6572[8408]; int unk1952; };
        struct { char _p6573[8412]; int unk1953; };
        struct { char _p6574[8416]; int unk1954; };
        struct { char _p6575[8420]; int unk1955; };
        struct { char _p6576[8424]; int unk1956; };
        struct { char _p6577[8428]; int unk1957; };
        struct { char _p6578[8432]; int unk1958; };
        struct { char _p6579[8436]; int unk1959; };
        struct { char _p6580[8440]; int unk1960; };
        struct { char _p6581[8444]; int unk1961; };
        struct { char _p6582[8448]; int unk1962; };
        struct { char _p6583[8452]; int unk1963; };
        struct { char _p6584[8456]; int unk1964; };
        struct { char _p6585[8460]; int unk1965; };
        struct { char _p6586[8464]; int unk1966; };
        struct { char _p6587[8468]; int unk1967; };
        struct { char _p6588[8472]; int unk1968; };
        struct { char _p6589[8476]; int unk1969; };
        struct { char _p6590[8480]; int unk1970; };
        struct { char _p6591[8484]; int unk1971; };
        struct { char _p6592[8488]; int unk1972; };
        struct { char _p6593[8492]; int unk1973; };
        struct { char _p6594[8496]; int unk1974; };
        struct { char _p6595[8500]; int unk1975; };
        struct { char _p6596[8504]; int unk1976; };
        struct { char _p6597[8508]; int unk1977; };
        struct { char _p6598[8512]; int unk1978; };
        struct { char _p6599[8516]; int unk1979; };
        struct { char _p6600[8520]; int unk1980; };
        struct { char _p6601[8524]; int unk1981; };
        struct { char _p6602[8528]; int unk1982; };
        struct { char _p6603[8532]; int unk1983; };
        struct { char _p6604[8536]; int unk1984; };
        struct { char _p6605[8540]; int unk1985; };
        struct { char _p6606[8544]; int unk1986; };
        struct { char _p6607[8548]; int unk1987; };
        struct { char _p6608[8552]; int unk1988; };
        struct { char _p6609[8556]; int unk1989; };
        struct { char _p6610[8560]; int unk1990; };
        struct { char _p6611[8564]; int unk1991; };
        struct { char _p6612[8568]; int unk1992; };
        struct { char _p6613[8572]; u32 TM_OSDEnabled; };
        struct { char _p6614[8576]; u8 TM_OSDPosition; };
        struct { char _p6615[8577]; u8 TM_EventPage; };
        struct { char _p6616[8578]; u8 TM_OSDRecommended; };
        struct { char _p6617[8579]; u8 TM_LabFrameAdvanceButton; };
        struct { char _p6618[8580]; u8 TM_LabDPadUD; };
        struct { char _p6619[8581]; u8 TM_LabDPadLR; };
        struct { char _p6620[8582]; u8 unused1F2E; };
        struct { char _p6621[8583]; u8 TM_LabCPUInputDisplay; };
        struct { char _p6622[8584]; OverlaySave TM_LabSavedOverlays_HMN[8]; };
        struct { char _p6623[8600]; OverlaySave TM_LabSavedOverlays_CPU[8]; };
        struct { char _p6624[8616]; int unk2004; };
        struct { char _p6625[8620]; int unk2005; };
        struct { char _p6626[8624]; int unk2006; };
        struct { char _p6627[8628]; int unk2007; };
        struct { char _p6628[8632]; int unk2008; };
        struct { char _p6629[8636]; int unk2009; };
        struct { char _p6630[8640]; int unk2010; };
        struct { char _p6631[8644]; int unk2011; };
        struct { char _p6632[8648]; int unk2012; };
        struct { char _p6633[8652]; int unk2013; };
        struct { char _p6634[8656]; int unk2014; };
        struct { char _p6635[8660]; int unk2015; };
        struct { char _p6636[8664]; int unk2016; };
        struct { char _p6637[8668]; int unk2017; };
        struct { char _p6638[8672]; int unk2018; };
        struct { char _p6639[8676]; int unk2019; };
        struct { char _p6640[8680]; int unk2020; };
        struct { char _p6641[8684]; int unk2021; };
        struct { char _p6642[8688]; int unk2022; };
        struct { char _p6643[8692]; int unk2023; };
        struct { char _p6644[8696]; int unk2024; };
        struct { char _p6645[8700]; int unk2025; };
        struct { char _p6646[8704]; int unk2026; };
        struct { char _p6647[8708]; int unk2027; };
        struct { char _p6648[8712]; int unk2028; };
        struct { char _p6649[8716]; int unk2029; };
        struct { char _p6650[8720]; int unk2030; };
        struct { char _p6651[8724]; int unk2031; };
        struct { char _p6652[8728]; int unk2032; };
        struct { char _p6653[8732]; int unk2033; };
        struct { char _p6654[8736]; int unk2034; };
        struct { char _p6655[8740]; int unk2035; };
        struct { char _p6656[8744]; int unk2036; };
        struct { char _p6657[8748]; int unk2037; };
        struct { char _p6658[8752]; int unk2038; };
        struct { char _p6659[8756]; int unk2039; };
        struct { char _p6660[8760]; int unk2040; };
        struct { char _p6661[8764]; int unk2041; };
        struct { char _p6662[8768]; int unk2042; };
        struct { char _p6663[8772]; int unk2043; };
        struct { char _p6664[8776]; int unk2044; };
        struct { char _p6665[8780]; int unk2045; };
        struct { char _p6666[8784]; int unk2046; };
        struct { char _p6667[8788]; int unk2047; };
    };
};

struct SnapshotInfo
{
    int snap_id;
    u16 is_exist;
    u16 block_size;
};

struct SnapshotList
{
    int x0;
    int snap_num;
    int x8;
    int xc;
    SnapshotInfo snap_info[128];
};

struct MemSnapIconData
{
    _HSD_ImageDesc *banner;
    _HSD_ImageDesc *icon;
};

struct MemSaveIconData
{
    _HSD_ImageDesc *banner;
    _HSD_ImageDesc *icon;
    _HSD_ImageDesc *all_banner; // unlocked all
    _HSD_ImageDesc *all_icon;   // unlocked all
};

struct MemcardSave
{
    int size;   // size of the data, only used when writing to the card
    int x4;     // unknown, is usually 3
    void *data; // pointer to the save data
    int xc;     // is -1 to signify end of data
};

struct MemcardWork
{

    void *work_area;             // 0x0
    void *buffer;                // 0x4, temp transfer buffer used during CARDReads. is 0x2000 bytes
    int x8;                      // 0x8
    int xc;                      // 0xc
    int x10;                     // 0x10
    int x14;                     // 0x14
    void *icon_data;             // 0x18
    void *banner_data;           // 0x1c
    int x20;                     // 0x20
    int x24;                     // 0x24
    int x28;                     // 0x28
    int x2c;                     // 0x2c
    int x30;                     // 0x30
    int x34;                     // 0x34
    int x38;                     // 0x38
    int x3c;                     // 0x3c
    int x40;                     // 0x40
    int x44;                     // 0x44
    int x48;                     // 0x48
    int x4c;                     // 0x4c
    int x50;                     // 0x50
    int x54;                     // 0x54
    int x58;                     // 0x58
    int x5c;                     // 0x5c
    int x60;                     // 0x60
    int x64;                     // 0x64
    int x68;                     // 0x68
    int x6c;                     // 0x6c
    int x70;                     // 0x70
    int x74;                     // 0x74
    int x78;                     // 0x78
    int x7c;                     // 0x7c
    int x80;                     // 0x80
    int x84;                     // 0x84
    int x88;                     // 0x88
    int x8c;                     // 0x8c
    int x90;                     // 0x90
    CARDFileInfo card_file_x94;  // 0x94
    void *buffer_use;            // 0xa8, temp transfer buffer used during CARDReads
    int xac;                     // 0xac
    int buffer_size;             // 0xb0
    CARDFileInfo card_file_info; // 0xb4    cancel vanilla memcard reads with this fileinfo
    int xc8;                     // 0xc8
    int card_data_start;         // 0xcc, data after header and banner and metadata
    int xd0;                     // 0xd0
    int xd4;                     // 0xd4
    int xd8;                     // 0xd8
    int xdc;                     // 0xdc
    int xe0;                     // 0xe0
    int xe4;                     // 0xe4
    int xe8;                     // 0xe8
    int xec;                     // 0xec
    int xf0;                     // 0xf0
    int xf4;                     // 0xf4
    int xf8;                     // 0xf8
    int xfc;                     // 0xfc
    int x100;                    // 0x100
    int x104;                    // 0x104
    int x108;                    // 0x108
    int x10c;                    // 0x10c
    int x110;                    // 0x110
    int x114;                    // 0x114
    int x118;                    // 0x118
    int x11c;                    // 0x11c
    int x120;                    // 0x120
    int x124;                    // 0x124
    int x128;                    // 0x128
    int x12c;                    // 0x12c
    int x130;                    // 0x130
    int x134;                    // 0x134
    int x138;                    // 0x138
    int x13c;                    // 0x13c
    int x140;                    // 0x140
    int x144;                    // 0x144
    int x148;                    // 0x148
    int x14c;                    // 0x14c
    int x150;                    // 0x150
    int x154;                    // 0x154
    int x158;                    // 0x158
    int x15c;                    // 0x15c
    int x160;                    // 0x160
    int x164;                    // 0x164
    int x168;                    // 0x168
    int x16c;                    // 0x16c
    int x170;                    // 0x170
    int x174;                    // 0x174
    int x178;                    // 0x178
    int x17c;                    // 0x17c
    int x180;                    // 0x180
    int x184;                    // 0x184
    int x188;                    // 0x188
    int x18c;                    // 0x18c
    int x190;                    // 0x190
    int x194;                    // 0x194
    int x198;                    // 0x198
    int x19c;                    // 0x19c
    int x1a0;                    // 0x1a0
    int x1a4;                    // 0x1a4
    int x1a8;                    // 0x1a8
    int x1ac;                    // 0x1ac
    int x1b0;                    // 0x1b0
    int x1b4;                    // 0x1b4
    int x1b8;                    // 0x1b8
    int x1bc;                    // 0x1bc
    int x1c0;                    // 0x1c0
    int x1c4;                    // 0x1c4
    int x1c8;                    // 0x1c8
    int x1cc;                    // 0x1cc
    int x1d0;                    // 0x1d0
    int x1d4;                    // 0x1d4
    int x1d8;                    // 0x1d8
    int x1dc;                    // 0x1dc
    int x1e0;                    // 0x1e0
    int x1e4;                    // 0x1e4
    int x1e8;                    // 0x1e8
    int x1ec;                    // 0x1ec
    int x1f0;                    // 0x1f0
    int x1f4;                    // 0x1f4
    int x1f8;                    // 0x1f8
    int x1fc;                    // 0x1fc
    int x200;                    // 0x200
    int x204;                    // 0x204
    int x208;                    // 0x208
    int x20c;                    // 0x20c
    int x210;                    // 0x210
    int x214;                    // 0x214
    int x218;                    // 0x218
    int x21c;                    // 0x21c
    int x220;                    // 0x220
    int x224;                    // 0x224
    int x228;                    // 0x228
    int x22c;                    // 0x22c
    int x230;                    // 0x230
    int x234;                    // 0x234
    int x238;                    // 0x238
    int x23c;                    // 0x23c
    int x240;                    // 0x240
    int x244;                    // 0x244
    int x248;                    // 0x248
    int x24c;                    // 0x24c
    int x250;                    // 0x250
    int x254;                    // 0x254
    int x258;                    // 0x258
    int x25c;                    // 0x25c
    int x260;                    // 0x260
    int x264;                    // 0x264
    int x268;                    // 0x268
    int x26c;                    // 0x26c
    int x270;                    // 0x270
    int x274;                    // 0x274
    int x278;                    // 0x278
    int x27c;                    // 0x27c
    int x280;                    // 0x280
    int x284;                    // 0x284
    int x288;                    // 0x288
    int x28c;                    // 0x28c
    int x290;                    // 0x290
    int x294;                    // 0x294
    int x298;                    // 0x298
    int x29c;                    // 0x29c
    int x2a0;                    // 0x2a0
    int x2a4;                    // 0x2a4
    int x2a8;                    // 0x2a8
    int x2ac;                    // 0x2ac
    int x2b0;                    // 0x2b0
    int x2b4;                    // 0x2b4
    int x2b8;                    // 0x2b8
    int x2bc;                    // 0x2bc
    int x2c0;                    // 0x2c0
    int x2c4;                    // 0x2c4
    int x2c8;                    // 0x2c8
    int x2cc;                    // 0x2cc
    int x2d0;                    // 0x2d0
    int x2d4;                    // 0x2d4
    int x2d8;                    // 0x2d8
    int x2dc;                    // 0x2dc
    int x2e0;                    // 0x2e0
    int x2e4;                    // 0x2e4
    int x2e8;                    // 0x2e8
    int x2ec;                    // 0x2ec
    int x2f0;                    // 0x2f0
    int x2f4;                    // 0x2f4
    int x2f8;                    // 0x2f8
    int x2fc;                    // 0x2fc
    int x300;                    // 0x300
    int x304;                    // 0x304
    int x308;                    // 0x308
    int x30c;                    // 0x30c
    int x310;                    // 0x310
    int x314;                    // 0x314
    int x318;                    // 0x318
    int x31c;                    // 0x31c
    int x320;                    // 0x320
    int x324;                    // 0x324
    int x328;                    // 0x328
    int x32c;                    // 0x32c
    int x330;                    // 0x330
    int x334;                    // 0x334
    int x338;                    // 0x338
    int x33c;                    // 0x33c
    int x340;                    // 0x340
    int x344;                    // 0x344
    int x348;                    // 0x348
    int x34c;                    // 0x34c
    int x350;                    // 0x350
    int x354;                    // 0x354
    int x358;                    // 0x358
    int x35c;                    // 0x35c
    int x360;                    // 0x360
    int x364;                    // 0x364
    int x368;                    // 0x368
    int x36c;                    // 0x36c
    int x370;                    // 0x370
    int x374;                    // 0x374
    int x378;                    // 0x378
    int x37c;                    // 0x37c
    int x380;                    // 0x380
    int x384;                    // 0x384
    int x388;                    // 0x388
    int x38c;                    // 0x38c
    int x390;                    // 0x390
    int x394;                    // 0x394
    int x398;                    // 0x398
    int x39c;                    // 0x39c
    int x3a0;                    // 0x3a0
    int x3a4;                    // 0x3a4
    int x3a8;                    // 0x3a8
    int x3ac;                    // 0x3ac
    int x3b0;                    // 0x3b0
    int x3b4;                    // 0x3b4
    int x3b8;                    // 0x3b8
    int x3bc;                    // 0x3bc
    int x3c0;                    // 0x3c0
    int x3c4;                    // 0x3c4
    int x3c8;                    // 0x3c8
    int x3cc;                    // 0x3cc
    int x3d0;                    // 0x3d0
    int x3d4;                    // 0x3d4
    int x3d8;                    // 0x3d8
    int x3dc;                    // 0x3dc
    int x3e0;                    // 0x3e0
    int x3e4;                    // 0x3e4
    int x3e8;                    // 0x3e8
    int x3ec;                    // 0x3ec
    int x3f0;                    // 0x3f0
    int x3f4;                    // 0x3f4
    int x3f8;                    // 0x3f8
    int x3fc;                    // 0x3fc
    int x400;                    // 0x400
    int x404;                    // 0x404
    int x408;                    // 0x408
    int x40c;                    // 0x40c
    int x410;                    // 0x410
    int x414;                    // 0x414
    int x418;                    // 0x418
    int x41c;                    // 0x41c
    int x420;                    // 0x420
    int x424;                    // 0x424
    int x428;                    // 0x428
    int x42c;                    // 0x42c
    int x430;                    // 0x430
    int x434;                    // 0x434
    int x438;                    // 0x438
    int x43c;                    // 0x43c
    int x440;                    // 0x440
    int x444;                    // 0x444
    int x448;                    // 0x448
    int x44c;                    // 0x44c
    int x450;                    // 0x450
    int x454;                    // 0x454
    int x458;                    // 0x458
    int x45c;                    // 0x45c
    int x460;                    // 0x460
    int x464;                    // 0x464
    int x468;                    // 0x468
    int x46c;                    // 0x46c
    int x470;                    // 0x470
    int x474;                    // 0x474
    int x478;                    // 0x478
    int x47c;                    // 0x47c
    int x480;                    // 0x480
    int x484;                    // 0x484
    int x488;                    // 0x488
    int x48c;                    // 0x48c
    int x490;                    // 0x490
    int x494;                    // 0x494
    int x498;                    // 0x498
    int x49c;                    // 0x49c
    int x4a0;                    // 0x4a0
    int x4a4;                    // 0x4a4
    int x4a8;                    // 0x4a8
    int x4ac;                    // 0x4ac
    int x4b0;                    // 0x4b0
    int x4b4;                    // 0x4b4
    int x4b8;                    // 0x4b8
    int x4bc;                    // 0x4bc
    int x4c0;                    // 0x4c0
    int x4c4;                    // 0x4c4
    int x4c8;                    // 0x4c8
    int x4cc;                    // 0x4cc
    int x4d0;                    // 0x4d0
    int x4d4;                    // 0x4d4
    int x4d8;                    // 0x4d8
    int x4dc;                    // 0x4dc
    int x4e0;                    // 0x4e0
    int x4e4;                    // 0x4e4
    int x4e8;                    // 0x4e8
    int x4ec;                    // 0x4ec
    int x4f0;                    // 0x4f0
    int x4f4;                    // 0x4f4
    int x4f8;                    // 0x4f8
    int x4fc;                    // 0x4fc
    int x500;                    // 0x500
    int x504;                    // 0x504
    int x508;                    // 0x508
    int x50c;                    // 0x50c
    int x510;                    // 0x510
    int x514;                    // 0x514
    int x518;                    // 0x518
    int x51c;                    // 0x51c
    int x520;                    // 0x520
    int x524;                    // 0x524
    int x528;                    // 0x528
    int x52c;                    // 0x52c
    int x530;                    // 0x530
    int x534;                    // 0x534
    int x538;                    // 0x538
    int x53c;                    // 0x53c
    int x540;                    // 0x540
    int x544;                    // 0x544
    int x548;                    // 0x548
    int x54c;                    // 0x54c
    int x550;                    // 0x550
    int x554;                    // 0x554
    int x558;                    // 0x558
    int x55c;                    // 0x55c
    int x560;                    // 0x560
    int x564;                    // 0x564
    int x568;                    // 0x568
    int x56c;                    // 0x56c
    int x570;                    // 0x570
    int x574;                    // 0x574
    int x578;                    // 0x578
    int x57c;                    // 0x57c
    int x580;                    // 0x580
    int x584;                    // 0x584
    int x588;                    // 0x588
    int x58c;                    // 0x58c
    int x590;                    // 0x590
    int x594;                    // 0x594
    int x598;                    // 0x598
    int x59c;                    // 0x59c
    int x5a0;                    // 0x5a0
    int x5a4;                    // 0x5a4
    int x5a8;                    // 0x5a8
    int x5ac;                    // 0x5ac
    int x5b0;                    // 0x5b0
    int x5b4;                    // 0x5b4
    int x5b8;                    // 0x5b8
    int x5bc;                    // 0x5bc
    int x5c0;                    // 0x5c0
    int x5c4;                    // 0x5c4
    int x5c8;                    // 0x5c8
    int x5cc;                    // 0x5cc
    int x5d0;                    // 0x5d0
    int x5d4;                    // 0x5d4
    int x5d8;                    // 0x5d8
    int x5dc;                    // 0x5dc
    int x5e0;                    // 0x5e0
    int x5e4;                    // 0x5e4
    int x5e8;                    // 0x5e8
    int x5ec;                    // 0x5ec
    int x5f0;                    // 0x5f0
    int x5f4;                    // 0x5f4
    int x5f8;                    // 0x5f8
    int x5fc;                    // 0x5fc
    int x600;                    // 0x600
    int x604;                    // 0x604
    int x608;                    // 0x608
    int x60c;                    // 0x60c
    int x610;                    // 0x610
    int x614;                    // 0x614
    int x618;                    // 0x618
    int x61c;                    // 0x61c
    int x620;                    // 0x620
    int x624;                    // 0x624
    int x628;                    // 0x628
    int x62c;                    // 0x62c
    int x630;                    // 0x630
    int x634;                    // 0x634
    int x638;                    // 0x638
    int x63c;                    // 0x63c
    int x640;                    // 0x640
    int x644;                    // 0x644
    int x648;                    // 0x648
    int x64c;                    // 0x64c
    int x650;                    // 0x650
    int x654;                    // 0x654
    int x658;                    // 0x658
    int x65c;                    // 0x65c
    int x660;                    // 0x660
    int x664;                    // 0x664
    int x668;                    // 0x668
    int x66c;                    // 0x66c
    int x670;                    // 0x670
    int x674;                    // 0x674
    int x678;                    // 0x678
    int x67c;                    // 0x67c
    int x680;                    // 0x680
    int x684;                    // 0x684
    int x688;                    // 0x688
    int x68c;                    // 0x68c
    int x690;                    // 0x690
    int x694;                    // 0x694
    int x698;                    // 0x698
    int x69c;                    // 0x69c
    int x6a0;                    // 0x6a0
    int x6a4;                    // 0x6a4
    int x6a8;                    // 0x6a8
    int x6ac;                    // 0x6ac
    int x6b0;                    // 0x6b0
    int x6b4;                    // 0x6b4
    int x6b8;                    // 0x6b8
    int x6bc;                    // 0x6bc
    int x6c0;                    // 0x6c0
    int x6c4;                    // 0x6c4
    int x6c8;                    // 0x6c8
    int x6cc;                    // 0x6cc
    int x6d0;                    // 0x6d0
    int x6d4;                    // 0x6d4
    int x6d8;                    // 0x6d8
    int x6dc;                    // 0x6dc
    int x6e0;                    // 0x6e0
    int x6e4;                    // 0x6e4
    int x6e8;                    // 0x6e8
    int x6ec;                    // 0x6ec
    int x6f0;                    // 0x6f0
    int x6f4;                    // 0x6f4
    int x6f8;                    // 0x6f8
    int x6fc;                    // 0x6fc
    int x700;                    // 0x700
    int x704;                    // 0x704
    int x708;                    // 0x708
    int x70c;                    // 0x70c
    int x710;                    // 0x710
    int x714;                    // 0x714
    int x718;                    // 0x718
    int x71c;                    // 0x71c
    int x720;                    // 0x720
    int x724;                    // 0x724
    int x728;                    // 0x728
    int x72c;                    // 0x72c
    int x730;                    // 0x730
    int x734;                    // 0x734
    int x738;                    // 0x738
    int x73c;                    // 0x73c
    int x740;                    // 0x740
    int x744;                    // 0x744
    int x748;                    // 0x748
    int x74c;                    // 0x74c
    int x750;                    // 0x750
    int x754;                    // 0x754
    int x758;                    // 0x758
    int x75c;                    // 0x75c
    int x760;                    // 0x760
    int x764;                    // 0x764
    int x768;                    // 0x768
    int x76c;                    // 0x76c
    int x770;                    // 0x770
    int x774;                    // 0x774
    int x778;                    // 0x778
    int x77c;                    // 0x77c
    int x780;                    // 0x780
    int x784;                    // 0x784
    int x788;                    // 0x788
    int x78c;                    // 0x78c
    int x790;                    // 0x790
    int x794;                    // 0x794
    int x798;                    // 0x798
    int x79c;                    // 0x79c
    int x7a0;                    // 0x7a0
    int x7a4;                    // 0x7a4
    int x7a8;                    // 0x7a8
    int x7ac;                    // 0x7ac
    int x7b0;                    // 0x7b0
    int x7b4;                    // 0x7b4
    int x7b8;                    // 0x7b8
    int x7bc;                    // 0x7bc
    int x7c0;                    // 0x7c0
    int x7c4;                    // 0x7c4
    int x7c8;                    // 0x7c8
    int x7cc;                    // 0x7cc
    int x7d0;                    // 0x7d0
    int x7d4;                    // 0x7d4
    int x7d8;                    // 0x7d8
    int x7dc;                    // 0x7dc
    int x7e0;                    // 0x7e0
    int x7e4;                    // 0x7e4
    int x7e8;                    // 0x7e8
    int x7ec;                    // 0x7ec
    int x7f0;                    // 0x7f0
    int x7f4;                    // 0x7f4
    int x7f8;                    // 0x7f8
    int x7fc;                    // 0x7fc
    int x800;                    // 0x800
    int x804;                    // 0x804
    int x808;                    // 0x808
    int x80c;                    // 0x80c
    int x810;                    // 0x810
    int x814;                    // 0x814
    int x818;                    // 0x818
    int x81c;                    // 0x81c
    int x820;                    // 0x820
    int x824;                    // 0x824
    int x828;                    // 0x828
    int x82c;                    // 0x82c
    int x830;                    // 0x830
    int x834;                    // 0x834
    int x838;                    // 0x838
    int x83c;                    // 0x83c
    int x840;                    // 0x840
    int x844;                    // 0x844
    int x848;                    // 0x848
    int x84c;                    // 0x84c
    int x850;                    // 0x850
    int x854;                    // 0x854
    int x858;                    // 0x858
    int x85c;                    // 0x85c
    int x860;                    // 0x860
    int x864;                    // 0x864
    int x868;                    // 0x868
    int x86c;                    // 0x86c
    int x870;                    // 0x870
    int x874;                    // 0x874
    int x878;                    // 0x878
    int x87c;                    // 0x87c
    int x880;                    // 0x880
    int x884;                    // 0x884
    int x888;                    // 0x888
    int x88c;                    // 0x88c
    int x890;                    // 0x890
    int x894;                    // 0x894
    int x898;                    // 0x898
    int x89c;                    // 0x89c
    int x8a0;                    // 0x8a0
    int x8a4;                    // 0x8a4
    int x8a8;                    // 0x8a8
    int is_done;                 // 0x8ac. operation callback sets this to 1 when the operation finishes
    // NOTE: i dont think anything past this is actually part of this struct
    int x8b0;                        // 0x8b0
    int x8b4;                        // 0x8b4
    int x8b8;                        // 0x8b8
    int x8bc;                        // 0x8bc
    int x8c0;                        // 0x8c0
    int x8c4;                        // 0x8c4
    int x8c8;                        // 0x8c8
    int x8cc;                        // 0x8cc
    int x8d0;                        // 0x8d0
    int x8d4;                        // 0x8d4
    int x8d8;                        // 0x8d8
    int x8dc;                        // 0x8dc
    int x8e0;                        // 0x8e0
    int x8e4;                        // 0x8e4
    int x8e8;                        // 0x8e8
    int x8ec;                        // 0x8ec
    int x8f0;                        // 0x8f0
    int x8f4;                        // 0x8f4
    int x8f8;                        // 0x8f8
    int x8fc;                        // 0x8fc
    int x900;                        // 0x900
    int x904;                        // 0x904
    int x908;                        // 0x908
    MemSaveIconData *save_icon_data; // 0x90C
};

struct MemcardUnk
{
    u8 block_size;
    u8 x1;
    u8 x2;
    u8 x3;
    u8 x4;
    u8 x5;
    u8 x6;
    u8 x7;
    u8 x8;
    u8 x9;
    u8 xa[8];
};

struct MemcardState
{
    /* +0  */ int x0;
    /* +4  */ int x4;
    /* +8  */ int x8;
    /* +C  */ bool memcard_changed;
    /* +10 */ int x10;
    /* +14 */ int x14;
    /* +18 */ bool enable;
    /* +1C */ char _1C[0x40];
    /* +5C */ int *x5C;
    /* +60 */ int x60;
    /* +64 */ void *x64;
};

struct MemcardInfo
{
    void *snap_data;                 // 0x0 (should be 256,064 bytes)
    char file_name[32];              // should end with spaces
    char file_desc[32];              // should end with spaces
    MemSnapIconData *snap_icon_data; // 0x44
    SnapshotList *snap_list;         // 0x48 (should be 2112 bytes) points to an allocation where info on the snapshots present on slot A exists
    int memcard_probe;               // 0x4c 1 == is present. is updated every controller poll
    int x50;                         // 0x50
    int x54;                         // 0x54
    int x58;                         // 0x58
    int x5c;                         // 0x5c
    int x60;                         // 0x60
    int x64;                         // 0x64
    int x68;                         // 0x68
    int x6c;                         // 0x6c
    int x70;                         // 0x70
    int x74;                         // 0x74
    int x78;                         // 0x78
    int x7c;                         // 0x7c
    int x80;                         // 0x80
    int x84;                         // 0x84
    int x88;                         // 0x88
    int x8c;                         // 0x8c
    int x90;                         // 0x90
    int x94;                         // 0x94
    int x98;                         // 0x98
    int x9c;                         // 0x9c
    int xa0;                         // 0xa0
    int xa4;                         // 0xa4
    int xa8;                         // 0xa8
    int xac;                         // 0xac
    int xb0;                         // 0xb0
    int xb4;                         // 0xb4
    int xb8;                         // 0xb8
    int xbc;                         // 0xbc
    int xc0;                         // 0xc0
    int xc4;                         // 0xc4
    int xc8;                         // 0xc8
    int xcc;                         // 0xcc
    int xd0;                         // 0xd0
    int xd4;                         // 0xd4
    int xd8;                         // 0xd8
    int xdc;                         // 0xdc
    int xe0;                         // 0xe0
    int xe4;                         // 0xe4
    int xe8;                         // 0xe8
    int xec;                         // 0xec
};

struct Rules1
{
    u8 x0;
    u8 x1;
    u8 match_kind;      // 0x2
    u8 time;            // 0x3
    u8 stock_num;       // 0x4
    u8 handicap;        // 0x5
    u8 dmg_ratio;       // 0x6
    u8 stage_selection; // 0x7
    u8 stock_time;      // 0x8
    u8 friendly_fire;   // 0x9
    u8 pause;           // 0xa
    u8 score_display;   // 0xb
    u8 self_destruct;   // 0xc
};

struct Rules4
{
    u64 x0;           // 0x0
    u64 item_switch;  // 0x8
    int x10;          // 0x10
    u8 x14;           // 0x14
    u8 x15;           // 0x15
    u8 language;      // 0x16
    u32 stage_switch; // 0x18
};

/*** Static Variables ***/
// static MemcardState *stc_memcard_state = (void *)0x80433318;
extern void *mu_tmce_ref_stc_memcard_state;
#define stc_memcard_state ((MemcardState *)((char *)mu_tmce_ref_stc_memcard_state + 0))
// static MemcardInfo *stc_memcard_info = (void *)0x80433380;
extern void *mu_tmce_ref_stc_memcard_info;
#define stc_memcard_info ((MemcardInfo *)((char *)mu_tmce_ref_stc_memcard_info + 0))
// static MemcardUnk *stc_memcard_unk = (void *)0x803bacc8;
extern void *mu_tmce_ref_stc_memcard_unk;
#define stc_memcard_unk ((MemcardUnk *)((char *)mu_tmce_ref_stc_memcard_unk + 0))
// static MemcardWork *stc_memcard_work = (void *)0x80432a68;
extern void *mu_tmce_ref_stc_memcard_work;
#define stc_memcard_work ((MemcardWork *)((char *)mu_tmce_ref_stc_memcard_work + 0))
extern char mu_mx_gmMainLib_8045A6C0[] __asm__("gmMainLib_8045A6C0");
static Memcard *stc_memcard = (void *)(mu_mx_gmMainLib_8045A6C0 + 0); // pointer to this data @ 0x804d3ee0
// static int *stc_memcard_block_curr = R13_OFFSET(-0x3d20);
extern void *mu_tmce_ref_stc_memcard_block_curr;
#define stc_memcard_block_curr ((int *)((char *)mu_tmce_ref_stc_memcard_block_curr + 0))
// static int *stc_memcard_block_last = R13_OFFSET(-0x3d1c);
extern void *mu_tmce_ref_stc_memcard_block_last;
#define stc_memcard_block_last ((int *)((char *)mu_tmce_ref_stc_memcard_block_last + 0))
// static int *stc_memcard_write_status = (void *)0x804d1138;
extern void *mu_tmce_ref_stc_memcard_write_status;
#define stc_memcard_write_status ((int *)((char *)mu_tmce_ref_stc_memcard_write_status + 0))
// static int *stc_CardXferredBytes = R13_OFFSET(-0x3D14);
extern void *mu_tmce_ref_stc_CardXferredBytes;
#define stc_CardXferredBytes ((int *)((char *)mu_tmce_ref_stc_CardXferredBytes + 0))

/*** Memcard Library ***/
void Memcard_InitWorkArea();
void Memcard_LoadAssets(int unk);
Rules1 *Memcard_GetRules1();
Rules4 *Memcard_GetRules4();
void Memcard_InitSnapshotList(void *snap_data, void *snap_list);
void Memcard_UpdateSnapshotList(int slot);
void Memcard_ReqSaveCreate(int slot, char *file_name, MemcardSave *memcard_save, MemcardUnk *memcard_unk, char *save_name, _HSD_ImageDesc *banner, _HSD_ImageDesc *icon, int unk);
void Memcard_DeleteSnapshot(int slot, int index);
void Memcard_ReqSaveLoad(int slot, char *file_name, MemcardSave *memcard_save, char *save_name, _HSD_ImageDesc *banner, _HSD_ImageDesc *icon, int unk);
void Memcard_ReqSaveUpdate(int slot, char *file_name, MemcardSave *memcard_save, char *save_name, _HSD_ImageDesc *banner, _HSD_ImageDesc *icon, void *r9, void *cb);
int Memcard_CheckStatus(); // returns 11 when operation in effect
void Memcard_RemovedCallback();
void Memcard_Obfuscate(void *data, int size);
void Memcard_Deobfuscate(void *data, int size);
void Memcard_SaveIfChanged(void);

#endif
