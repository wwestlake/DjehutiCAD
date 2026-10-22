# Generates robotics-component-library.json (shared, byte-identical in DjehutiCAD and DEWB) and copies the
# manufacturer originals it references. Values marked "reviewed" are transcribed from the acquisition inventories;
# values recalled from public datasheets but not re-verified in this pass are "unverified".
param(
  [string]$Drive = 'G:\My Drive\000 DjehutiCAD\Manufacturer Components',
  [string]$CadRes = 'D:\DjehutiCAD\Resources',
  [string]$DewbAssets = 'D:\000 Tech Research\Electronics Design Tool\prototype\assets')
$ErrorActionPreference = 'Stop'
$files = (Get-Content (Join-Path $Drive 'Hobby_Robotics_Acquisition_Files.json') -Raw | ConvertFrom-Json).files
$extract = {param($fam,$sub) Join-Path (Join-Path $Drive $fam) $sub}
# id -> source descriptor. 'copy' is the file placed in manufacturer-foundation/.
$src = @{}
function Src($id,$path,$lookup,$rev,$memberOf){
  $f = $files | ? { $_.filename -eq $lookup } | select -first 1
  if(-not $f){ throw "no url for $lookup" }
  $h = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower()
  $script:src[$id] = [ordered]@{ id=$id; originalFilename=(Split-Path $path -Leaf); url=($f.url -replace ' ','%20'); revision=$rev;
    path='manufacturer-foundation/'+(Split-Path $path -Leaf); sha256=$h; local=$path }
}
$O='originals'
Src 'sg90-print' "$Drive\TowerPro\SG90 and MG90S\$O\sg90-mfr-print\SMY-SG90.pdf" 'SMY-SG90.pdf' 'Not stated; manufacturer print hosted by Mantech'
Src 'mg90s-image' "$Drive\TowerPro\SG90 and MG90S\$O\mg90s-manufacturer-housing\MG90S-1B.jpg" 'MG90S-1B.jpg' 'Not stated; TowerPro housing drawing image'
Src 'fs90r-v1' "$Drive\FEETECH\FS90R\$O\feetech-pdf\FS90R.pdf" 'FS90R.pdf' 'V1.0; publication date not stated'
Src 'fs90r-v2' "$Drive\FEETECH\FS90R\$O\feetech-v2-datasheet\FS90R_V2_0_specs-1915423.pdf" 'FS90R_V2_0_specs-1915423.pdf' 'V2.0; publication date not stated'
Src 'pololu-mm-dims' "$Drive\Pololu\Micro Metal Gearmotors\$O\pololu2365-page-asset1\micro-metal-gearmotors-dimensions.pdf" 'micro-metal-gearmotors-dimensions.pdf' 'Revision not determined'
$mmz='Pololu Micro Metal Gearmotor 3D %28STEP%29 Models.zip'
$mmx="$Drive\Pololu\Micro Metal Gearmotors\extracted\pololu2365-page-asset2\Pololu Micro Metal Gearmotor 3D %28STEP%29 Models"
Src 'pololu-mm-step-ext' "$mmx\Encoder-Compatible (Extended Motor Shaft)\mmgm-pm-ext-shaft.step" $mmz 'Member of Pololu STEP archive (revision not stated); #2379 per README'
Src 'pololu-mm-step-enc' "$mmx\With Encoder, Back Connector\mmgm-pm-enc-back-con.step" $mmz 'Member of Pololu STEP archive (revision not stated); #5134 per README'
Src 'fit0016-pdf' "$Drive\DFRobot\FIT0016 TT Gearmotor\$O\tt-datasheet\FIT0016_Web.pdf" 'FIT0016_Web.pdf' 'Not stated; DFRobot PDF hosted by DigiKey'
Src 'l12-step30' "$Drive\Actuonix\L12\extracted\actuonix-page-asset2\L12_STP\l12_30mm_in.stp" 'L12_STP.zip' 'Member of L12_STP.zip (undated family archive)'
Src 'c514-ds' "$Drive\Chaocheng\TAU0730TM-14 (Adafruit 412)\$O\adafruit412-page-asset2\C514-datasheet.pdf" 'C514-datasheet.pdf' 'Version A, 21/07/12 as printed'
Src 'c514-dwg' "$Drive\Chaocheng\TAU0730TM-14 (Adafruit 412)\$O\adafruit412-page-asset1\412_C514-B_diagram.PDF" '412_C514-B_diagram.PDF' 'Rev 1, 21.08.30 as printed'
Src 'as5600-ds' "$Drive\ams OSRAM\AS5600\$O\seeed-as5600-page-asset1\Magnetic Rotary Position Sensor AS5600 Datasheet.pdf" 'Magnetic Rotary Position Sensor AS5600 Datasheet.pdf' 'v1-06, 2018-Jun-20'
Src 'd2f-ds' "$Drive\Omron\D2F\$O\omron-distributor-datasheet\en_d2f-1664393.pdf" 'en_d2f-1664393.pdf' 'Catalog revision not determined'
Src 'bmi270-ds' "$Drive\Bosch Sensortec\BMI270\$O\bmi270-datasheet\bst-bmi270-ds000.pdf" 'bst-bmi270-ds000.pdf' 'BST-BMI270-DS000-08 rev 1.6, March 2026'
Src 'vl53-ds' "$Drive\STMicroelectronics\VL53L1X\$O\vl53l1x-datasheet\DS_vl53l1x.pdf" 'DS_vl53l1x.pdf' 'DS12385 Rev 8'
Src 'vl53-carrier-step' "$Drive\Pololu\3415 VL53L1X Carrier\$O\pololu3415-page-asset3\vl53l0x-vl53l1x-vl53l3cx-vl53l4cd-carrier-model.step" 'vl53l0x-vl53l1x-vl53l3cx-vl53l4cd-carrier-model.step' 'Revision not stated'
Src 'ezo-cat' "$Drive\EZO\623ZZ and 625ZZ\$O\ezo-public-catalog\EZO_spb_all_en_18.pdf" 'EZO_spb_all_en_18.pdf' 'Distributor filename en_18; release date not established'
$wz="$Drive\Pololu\1088 Wheel and Shaft Interfaces"
Src 'wheel-step' "$wz\extracted\pololu1088-page-asset1\pololu-wheel-32x7mm\pololu-wheel-32x7mm-white.step" 'pololu-wheel-32x7mm.zip' 'Member (white) of pololu-wheel-32x7mm.zip; revision not stated'
Src 'hub-step' "$wz\$O\pololu1078-page-asset1\pololu-universal-aluminum-mounting-hub-for-3mm-shaft-4-40-holes.step" 'pololu-universal-aluminum-mounting-hub-for-3mm-shaft-4-40-holes.step' 'Revision not stated'
Src 'hub-dwg' "$wz\$O\pololu1078-page-asset0\1078-3mm-4-40-hub-dimensions.pdf" '1078-3mm-4-40-hub-dimensions.pdf' 'Revision not stated'
Src 't8-ds' "$Drive\STEPPERONLINE\T8 Lead Screw\$O\t8-pdf-referer\LSN-S-MB-320_Full_Datasheet.pdf" 'LSN-S-MB-320_Full_Datasheet.pdf' 'Drawing A4646, 6.11.2025 as printed'
# Sources that already live in the repo foundation (foundation commit).
function Existing($id,$file,$url,$rev){ $p=Join-Path "$CadRes\manufacturer-foundation" $file
  $script:src[$id]=[ordered]@{id=$id;originalFilename=$file;url=$url;revision=$rev;path="manufacturer-foundation/$file";sha256=(Get-FileHash $p -Algorithm SHA256).Hash.ToLower();local=$null} }
Existing 'pololu-mm-ds' 'pololu-micro-metal-gearmotors-rev-6-2.pdf' 'https://www.pololu.com/file/0J1487/pololu-micro-metal-gearmotors-rev-6-2.pdf' 'Rev 6.2, February 2026'
Existing 'l12-ds' 'ActuonixL12Datasheet.pdf' 'https://www.actuonix.com/assets/images/datasheets/ActuonixL12Datasheet.pdf' 'F (November 2019)'
Existing 'drv8833-ds' 'drv8833.pdf' 'https://www.ti.com/lit/ds/symlink/drv8833.pdf' 'SLVSAR1E, July 2015'

$MP='MP 6V; 51.45:1'
function Q($v,$u){ [ordered]@{quantity=$null;value=$v;unit=$u} }
function Claim($variant,$id,$label,$q,$val,$unit,$prov,$ver,$refs,$cond){
  if($null -eq $cond){$cond=[ordered]@{}}
  $c=[ordered]@{id=$id;label=$label;quantity=$q;value=$val;unit=$unit;conditions=$cond;variant=$variant;provenance=$prov;verification=$ver}
  if($refs){$c.sourceRefs=@($refs)}; $c }
function T($v){ [ordered]@{quantity='voltage';value=$v;unit='V'} }
# Record builder: $c is an array of claims; $models = array of @{id,scope,parameters}.
function Rec($id,$name,$mfr,$pn,$variant,$desc,$srcIds,$claims,$models,$curves,$geomLabel,$extra){
  $s=@($srcIds|%{ $x=$src[$_]; [ordered]@{id=$x.id;originalFilename=$x.originalFilename;url=$x.url;revision=$x.revision;path=$x.path;sha256=$x.sha256} })
  $m=@($models|%{[ordered]@{id=$_.id;parameters=@($_.parameters);curveRefs=@($_.curves);scope=$_.scope}})
  $r=[ordered]@{id=$id;name=$name;manufacturer=$mfr;partNumber=$pn;model=$pn;revision=1;origin='commercial';category='Manufacturer References';
    description=$desc;properties=@($claims);geometry=[ordered]@{kind='none';label=$geomLabel};
    engineering=[ordered]@{specification=[ordered]@{schemaVersion=1;variant=$variant;sources=$s;curves=@($curves);models=$m}}}
  if($extra){foreach($k in $extra.Keys){$r[$k]=$extra[$k]}}; $r }
function Unk($v,$id,$label,$q,$u){ Claim $v $id $label $q $null $u 'unknown' 'unverified' $null ([ordered]@{note='Not published in acquired originals'}) }
$Recs=New-Object System.Collections.ArrayList
function Add($r){ [void]$Recs.Add($r) }
$sp="Resistance/derived values are in the model scope text; only explicitly published numbers are claims."

# 1 TowerPro servos --------------------------------------------------------------------------------------
foreach($d in @(@{n='SG90';v='SG90 Analog';mass=9;t=.12;L=23;W=12.2;H=29;s='sg90-print';src='TowerPro SG90 Analog'},
                @{n='MG90S';v='MG90S digital';mass=13.4;t=.10;L=22.8;W=12.2;H=28.5;s='mg90s-image';src='TowerPro MG90S'})){
  $v=$d.v;$b=$d.s;$c4=[ordered]@{supply=(T 4.8)}
  $cl=@(
   (Claim $v 'mass' 'Mass' 'mass' $d.mass 'g' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'body_length' 'Body length (excl. flange/shaft)' 'length' $d.L 'mm' 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Manufacturer pages contain conflicting dimension entries'})),
   (Claim $v 'body_width' 'Body width' 'length' $d.W 'mm' 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Manufacturer pages contain conflicting dimension entries'})),
   (Claim $v 'overall_height' 'Overall height' 'length' $d.H 'mm' 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Manufacturer pages contain conflicting dimension entries'})),
   (Claim $v 'torque' 'Rated torque' 'torque' 1.8 'kgf cm' 'manufacturer_published' 'reviewed' @($b) $c4),
   (Claim $v 'transit_time' 'No-load transit time per 60 deg' 'time' $d.t 's' 'manufacturer_published' 'reviewed' @($b) $c4),
   (Claim $v 'deadband' 'Dead band width' 'time' 1 'us' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'supply_min' 'Operating voltage minimum' 'voltage' 4.8 'V' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Datasheet performance point; range not stated in acquired originals'})),
   (Claim $v 'pulse_min' 'Pulse width for full travel, low end' 'time' 500 'us' 'assumed' 'unverified' $null ([ordered]@{note='Typical hobby-servo range; TowerPro endpoints not published in acquired originals'})),
   (Claim $v 'pulse_max' 'Pulse width for full travel, high end' 'time' 2400 'us' 'assumed' 'unverified' $null ([ordered]@{note='Typical hobby-servo range; TowerPro endpoints not published in acquired originals'})),
   (Unk $v 'stall_current' 'Stall current' 'current' 'A'),(Unk $v 'travel_angle' 'Actual angular travel limit' 'ratio' '1'),
   (Unk $v 'armature_resistance' 'Motor winding resistance' 'resistance' 'ohm'),(Unk $v 'output_inertia' 'Output inertia' 'inertia' 'kg m2'))
  Add (Rec ('servo-positional-towerpro-'+$d.n.ToLower()) ("TowerPro "+$d.n+' micro servo') 'TowerPro' $d.n $v ('Micro positional servo. Dimension/speed/voltage entries conflict across TowerPro pages; spline, PWM endpoints and electrical dynamics unpublished. '+$sp) @($b) $cl `
    @(@{id='positional_servo_first_order';parameters=@('torque','transit_time','deadband','pulse_min','pulse_max');curves=@();scope='Bounded rate-limited positional servo: angle command from pulse width (assumed endpoints), slew from published 60 deg transit, torque saturation at rated torque, 1 us deadband. Not a closed-loop or electrical model.'}) @() 'Envelope reference only (manufacturer housing drawing); no solid supplied. Reconstruct from drawing dimensions.' $null)
}
# 2 FEETECH FS90R ---------------------------------------------------------------------------------------
$v='FS90R V1.0';$b='fs90r-v1'
Add (Rec 'servo-continuous-feetech-fs90r-v1' 'FEETECH FS90R V1.0 continuous-rotation servo' 'FEETECH' 'FS90R' $v 'Continuous-rotation micro servo; V1.0 datasheet values. V2.0 is a separate record (drawings/spline differ).' @($b) @(
  (Claim $v 'pulse_min' 'Command pulse minimum' 'time' 700 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'pulse_max' 'Command pulse maximum' 'time' 2300 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'pulse_neutral' 'Neutral (stop) pulse' 'time' 1500 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'deadband' 'Dead band' 'time' 90 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'speed_4v8' 'Unloaded speed' 'angularSpeed' 80 'rpm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 4.8)})),
  (Claim $v 'speed_6v' 'Unloaded speed' 'angularSpeed' 100 'rpm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 6)})),
  (Claim $v 'torque_4v8' 'Peak stall torque' 'torque' 1.3 'kgf cm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 4.8)})),
  (Claim $v 'torque_6v' 'Peak stall torque' 'torque' 1.5 'kgf cm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 6)})),
  (Claim $v 'stall_current_4v8' 'Stall current' 'current' 0.7 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 4.8)})),
  (Claim $v 'stall_current_6v' 'Stall current' 'current' 0.8 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 6)})),
  (Unk $v 'time_constant' 'Speed-command time constant' 'time' 's'),(Unk $v 'output_inertia' 'Output inertia' 'inertia' 'kg m2')) `
  @(@{id='continuous_servo_speed_command';parameters=@('pulse_min','pulse_max','pulse_neutral','deadband','speed_6v','torque_6v');curves=@();scope='Pulse offset from neutral maps linearly to speed (assumed interpolation, not measured) with deadband; torque-speed droop linear between published unloaded speed and stall torque.'}) @() 'Drawing reconstruction needed; V1.0 and V2.0 drawings differ.' $null)
$v='FS90R V2.0';$b='fs90r-v2'
Add (Rec 'servo-continuous-feetech-fs90r-v2' 'FEETECH FS90R V2.0 continuous-rotation servo' 'FEETECH' 'FS90R' $v 'Continuous-rotation micro servo; V2.0 datasheet. Only pulse timing transcribed; speed/torque tables must be read from the V2.0 PDF before use.' @($b) @(
  (Claim $v 'pulse_min' 'Command pulse minimum' 'time' 700 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'pulse_max' 'Command pulse maximum' 'time' 2300 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'pulse_neutral' 'Neutral (stop) pulse' 'time' 1500 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'deadband' 'Dead band' 'time' 90 'us' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'neutral_tolerance' 'Neutral tolerance' 'time' 45 'us' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='+/- value'})),
  (Unk $v 'speed_6v' 'Unloaded speed at 6 V' 'angularSpeed' 'rpm'),(Unk $v 'torque_6v' 'Peak stall torque at 6 V' 'torque' 'kgf cm')) `
  @(@{id='continuous_servo_speed_command';parameters=@('pulse_min','pulse_max','pulse_neutral','deadband');curves=@();scope='Same bounded command model as V1.0; speed/torque parameters unavailable in this record.'}) @() 'Drawing reconstruction needed.' $null)

# 3 Pololu micro gearmotors (2365 exists in manufacturer-reference-library.json) ---------------------------
foreach($d in @(@{pn='2379';v="$MP; extended motor shaft; no encoder";nm='Pololu 2379 50:1 Micro Metal Gearmotor MP 6V, extended motor shaft';st='pololu-mm-step-ext';enc=$false;desc='Same motor as #2365 with extended rear shaft for encoder hardware.'},
                @{pn='5134';v="$MP; 12 CPR encoder, back connector";nm='Pololu 5134 50:1 Micro Metal Gearmotor MP 6V with 12 CPR encoder';st='pololu-mm-step-enc';enc=$true;desc='Same motor as #2365 with integrated 12 CPR (motor-shaft) quadrature encoder, back connector.'})){
  $v=$d.v;$ds='pololu-mm-ds';$cd=[ordered]@{operatingPoint='Extrapolated stall; not a continuous operating rating';supply=(T 6)}
  $cl=@(
   (Claim $v 'stall_voltage' 'Stall voltage' 'voltage' 6 'V' 'manufacturer_published' 'reviewed' @($ds) $cd),
   (Claim $v 'stall_current' 'Stall current' 'current' 0.67 'A' 'manufacturer_published' 'reviewed' @($ds) $cd),
   (Claim $v 'stall_torque' 'Stall torque' 'torque' 5.4 'kgf mm' 'manufacturer_published' 'reviewed' @($ds) $cd),
   (Claim $v 'no_load_speed' 'Typical no-load speed (product page)' 'angularSpeed' 420 'rpm' 'manufacturer_published' 'reviewed' @($ds) ([ordered]@{supply=(T 6);note='Fitted plot shows about 390 rpm; kept separate'})),
   (Claim $v 'no_load_current' 'Typical no-load current (product page)' 'current' 0.07 'A' 'manufacturer_published' 'reviewed' @($ds) ([ordered]@{supply=(T 6);note='Fitted plot shows about 53 mA; kept separate'})),
   (Claim $v 'curve_speed_intercept' 'Plotted fit: no-load speed' 'angularSpeed' 390 'rpm' 'manufacturer_published' 'reviewed' @($ds) ([ordered]@{supply=(T 6);fit='speed=390-72*T[kgf mm]'})),
   (Claim $v 'gear_ratio' 'Gear ratio' 'ratio' 51.45 '1' 'manufacturer_published' 'reviewed' @($ds) ([ordered]@{shaft='Output relative to motor'})),
   (Unk $v 'winding_inductance' 'Winding inductance' 'inductance' 'H'),(Unk $v 'rotor_inertia' 'Rotor inertia' 'inertia' 'kg m2'),(Unk $v 'gear_efficiency' 'Gearbox efficiency' 'ratio' '%'))
  if($d.enc){ $cl+= (Claim $v 'encoder_cpr' 'Encoder cycles per motor-shaft revolution' 'ratio' 12 '1' 'manufacturer_published' 'reviewed' @($ds) ([ordered]@{note='Quadrature; 48 edges per motor revolution; not output-shaft counts'})) }
  $cv=@([ordered]@{id='torque_speed_current';sourceRefs=@($ds);variant=$v;axes=@(@{quantity='torque';unit='kgf mm'},@{quantity='angularSpeed';unit='rpm'},@{quantity='current';unit='A'});conditions=[ordered]@{supply=(T 6)};page=22;publishedFit='speed(rpm)=390-72*torque(kgf mm); current(A)=0.053+0.11*torque(kgf mm)';note='Original PDF plot for #2365/#2379/#5134; fit is source transcription.'})
  $mp=@('stall_current','stall_torque','no_load_speed','no_load_current','curve_speed_intercept','gear_ratio'); if($d.enc){$mp+='encoder_cpr'}
  $sc='Geared DC model (existing mech::addDcMotor, output-shaft equivalent): R=6V/Is, Kt=Ts/Is, Ke=(V-R*I0)/w0, B=Kt*I0/w0; L and J are assumptions. Stall values are manufacturer extrapolations.'
  if($d.enc){$sc+=' Encoder: 12 cycles/motor rev x 51.45 = 617.4 nominal output counts/rev (derived from rounded ratio).'}
  Add (Rec ('motor-dc-pololu-'+$d.pn) $d.nm 'Pololu' $d.pn $v $d.desc @($ds,'pololu-mm-dims',$d.st) $cl @(@{id='dc_gearmotor';parameters=$mp;curves=@('torque_speed_current');scope=$sc}) $cv 'Original manufacturer STEP; attach with Attach CAD / attachModel.' $null)
}
# 4 DFRobot FIT0016 -------------------------------------------------------------------------------------
$v='FIT0016 TT gearmotor, back shaft, L shape';$b='fit0016-pdf'
Add (Rec 'motor-dc-dfrobot-fit0016' 'DFRobot FIT0016 TT geared motor with back shaft' 'DFRobot' 'FIT0016' $v 'DFRobot-branded TT gearmotor; underlying factory undisclosed. Documentation reports 160 and 180 rpm no-load at 6 V; 160 rpm (current product page) is used. Stall data is catalog-level, not measured.' @($b) @(
  (Claim $v 'rated_voltage' 'Rated voltage' 'voltage' 6 'V' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'speed_6v' 'No-load speed' 'angularSpeed' 160 'rpm' 'manufacturer_published' 'unverified' @($b) ([ordered]@{supply=(T 6);note='Other DFRobot documentation states 180 rpm'})),
  (Claim $v 'speed_3v' 'No-load speed' 'angularSpeed' 90 'rpm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 3)})),
  (Claim $v 'no_load_current_6v' 'No-load current (maximum)' 'current' 0.17 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 6)})),
  (Claim $v 'no_load_current_3v' 'No-load current (maximum)' 'current' 0.14 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 3)})),
  (Claim $v 'gear_ratio' 'Gear ratio' 'ratio' 120 '1' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'max_torque' 'Maximum output torque' 'torque' 0.8 'kgf cm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Catalog maximum; not a measured stall torque'})),
  (Claim $v 'stall_current' 'Maximum stall current' 'current' 2.8 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Supply voltage for this datum is not specified'})),
  (Unk $v 'winding_inductance' 'Winding inductance' 'inductance' 'H'),(Unk $v 'rotor_inertia' 'Rotor inertia' 'inertia' 'kg m2')) `
  @(@{id='dc_gearmotor';parameters=@('rated_voltage','speed_6v','no_load_current_6v','gear_ratio','max_torque','stall_current');curves=@();scope='Preliminary output-equivalent DC model at 6 V (R=6V/2.8A assumes the stall datum is at 6 V; provisional).'}) @() 'No manufacturer solid or toleranced drawing; mounting/shaft reference incomplete.' $null)

# 5 Actuonix L12 variants (L12-50-100-6-P exists) ---------------------------------------------------------
foreach($d in @(@{pn='L12-50-100-6-R';v='L12-50-100-6-R (RC input)';stroke=50;ratio='100:1';sp=13;f=42;i=0.46;volt=6;mass=40;len=102;ct=''},
                @{pn='L12-30-210-12-P';v='L12-30-210-12-P (potentiometer feedback)';stroke=30;ratio='210:1';sp=6.5;f=80;i=0.246;volt=12;mass=34;len=82;ct=''})){
  $v=$d.v;$b='l12-ds';$cd=[ordered]@{supply=(T $d.volt)}
  $cl=@(
   (Claim $v 'stroke' 'Stroke' 'length' $d.stroke 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'no_load_speed' 'No-load speed' 'speed' $d.sp 'mm/s' 'manufacturer_published' 'reviewed' @($b) $cd),
   (Claim $v 'max_force' 'Maximum lifted force' 'force' $d.f 'N' 'manufacturer_published' 'reviewed' @($b) $cd),
   (Claim $v 'stall_current' 'Stall current' 'current' $d.i 'A' 'manufacturer_published' 'reviewed' @($b) $cd),
   (Claim $v 'supply_voltage' 'Rated voltage' 'voltage' $d.volt 'V' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'max_duty' 'Maximum duty cycle' 'ratio' 20 '%' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'backlash' 'Backlash' 'length' 0.2 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'mass' 'Mass' 'mass' $d.mass 'g' 'manufacturer_published' 'reviewed' @($b) $null),
   (Claim $v 'closed_length' 'Closed length, hole center to hole center' 'length' $d.len 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
   (Unk $v 'winding_resistance' 'Winding resistance' 'resistance' 'ohm'),(Unk $v 'moving_inertia' 'Moving inertia' 'inertia' 'kg m2'))
  $step= if($d.stroke -eq 30){'l12-step30'}else{$null}
  Add (Rec ('actuator-linear-actuonix-'+$d.pn.ToLower()) ("Actuonix "+$d.pn) 'Actuonix' $d.pn $v ("L12 miniature linear actuator, $($d.ratio) gearing. Current is a published stall endpoint, not a current-vs-force curve.") @($b) $cl `
    @(@{id='linear_actuator_force_speed';parameters=@('stroke','no_load_speed','max_force','stall_current','supply_voltage','backlash');curves=@();scope='Linear force-speed droop between no-load speed and max force; travel clamp 0..stroke; '+$(if($d.pn -like '*-R'){'RC pulse input x=stroke*(pulse-1ms)/1ms (nominal mapping).'}else{'0-5 V-class potentiometer feedback proportional to position (scale per datasheet).'})}) @() 'Manufacturer STEP envelope in the L12 archive (50 mm in/out already bundled; 30 mm file bundled for this variant).' $null)
}
# 6 Chaocheng solenoid -----------------------------------------------------------------------------------
$v='TAU0730TM-14 (Adafruit 412)';$b='c514-ds';$b2='c514-dwg'
Add (Rec 'solenoid-chaocheng-tau0730tm-14' 'Chaocheng TAU0730TM-14 solenoid (Adafruit 412)' 'Shanghai Chaocheng Electronic Technology' 'TAU0730TM-14' $v 'Push-pull open-frame solenoid sold as Adafruit #412. OEM drawing travel 4.9 mm; retail page lists 5.5 mm (unresolved).' @($b,$b2) @(
  (Claim $v 'voltage' 'Rated voltage' 'voltage' 12 'V' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'coil_resistance' 'Coil resistance' 'resistance' 40 'ohm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{temperature='20 C'})),
  (Claim $v 'rated_current' 'Rated current' 'current' 0.3 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 12)})),
  (Claim $v 'rated_power' 'Rated power' 'power' 3.6 'W' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{supply=(T 12)})),
  (Claim $v 'max_duty' 'Duty cycle' 'ratio' 50 '%' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'force_extended' 'Approximate initial force at 4 mm stroke' 'force' 0.5 'N' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{position='4 mm'})),
  (Claim $v 'force_retained' 'Minimum force at 0 mm' 'force' 5 'N' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{position='0 mm'})),
  (Claim $v 'travel' 'Travel (OEM drawing)' 'length' 4.9 'mm' 'manufacturer_published' 'unverified' @($b2) ([ordered]@{note='Retail listing states 5.5 mm'})),
  (Unk $v 'coil_inductance' 'Coil inductance' 'inductance' 'H'),(Unk $v 'spring_rate' 'Return-spring rate (N/mm)' 'ratio' '1')) `
  @(@{id='solenoid_coarse';parameters=@('voltage','coil_resistance','rated_current','max_duty','force_extended','force_retained','travel');curves=@();scope='Coarse actuator: coil R, duty/thermal flag, force linearly interpolated between published endpoints over travel. Predictive pull-in/release dynamics not supported (L(i,x), spring, mass unknown).'}) @() 'OEM drawing 10104-00073014 guides reconstruction; no solid supplied.' $null)

# 7 TI DRV8833 IC (carrier #2130 exists) -------------------------------------------------------------------
$v='DRV8833PWPR (HTSSOP-16 PWP)';$b='drv8833-ds'
Add (Rec 'driver-ti-drv8833pwpr' 'Texas Instruments DRV8833PWPR dual H-bridge driver' 'Texas Instruments' 'DRV8833PWPR' $v 'Bare IC (PWP HTSSOP-16). Pololu #2130 carrier is the separate record driver-pololu-2130.' @($b) @(
  (Claim $v 'vm_min' 'Motor supply minimum' 'voltage' 2.7 'V' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'vm_max' 'Motor supply maximum' 'voltage' 10.8 'V' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'i_rms_bridge' 'RMS current per bridge' 'current' 1.5 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Under stated thermal conditions'})),
  (Claim $v 'i_peak_bridge' 'Peak current per bridge' 'current' 2 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Under stated thermal conditions'})),
  (Claim $v 'i_rms_parallel' 'RMS current, paralleled bridges' 'current' 3 'A' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Paralleled bridges only'})),
  (Unk $v 'rds_on' 'High+low-side on resistance' 'resistance' 'ohm')) `
  @(@{id='h_bridge_ideal';parameters=@('vm_min','vm_max','i_rms_bridge','i_peak_bridge');curves=@();scope='Truth-table H-bridge with coast/brake/forward/reverse, VM window UVLO and current-limit approximations; no transistor-level fidelity.'}) @() 'Package drawing in datasheet; no standalone package STEP.' $null)

# 8 AS5600 -----------------------------------------------------------------------------------------------
$v='AS5600-ASOT SOIC-8';$b='as5600-ds'
Add (Rec 'sensor-ams-as5600' 'ams OSRAM AS5600 12-bit magnetic rotary position sensor' 'ams OSRAM' 'AS5600-ASOT' $v 'Bare SOIC-8 IC; Seeed Grove board is a separate record.' @($b) @(
  (Claim $v 'resolution_counts' 'Counts per revolution' 'ratio' 4096 '1' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='12-bit; ideal LSB = 360/4096 = 0.087890625 deg; resolution is not accuracy'})),
  (Claim $v 'supply_3v3_min' 'Supply minimum (3.3 V mode)' 'voltage' 3 'V' 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Recalled from datasheet; confirm against v1-06 table'})),
  (Claim $v 'supply_5v_max' 'Supply maximum (5 V mode)' 'voltage' 5.5 'V' 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Recalled from datasheet; confirm against v1-06 table'})),
  (Unk $v 'installed_magnet_field' 'Field at installed magnet pose (mT)' 'ratio' '1')) `
  @(@{id='angle_sensor_ideal';parameters=@('resolution_counts');curves=@();scope='Ideal 12-bit angle quantizer with register/output-mode wrapper; magnetic misalignment, filters/hysteresis not calibrated.'}) @() 'SOIC-8 drawing only; reconstruct from datasheet.' $null)
$v='Seeed Grove SKU 101020692'
Add (Rec 'module-seeed-grove-as5600' 'Seeed Studio Grove 12-bit Magnetic Rotary Position Sensor (AS5600)' 'Seeed Studio' '101020692' $v 'Board carrying AS5600; Eagle sources acquired, no STEP.' @($b) @(
  (Claim $v 'resolution_counts' 'Counts per revolution' 'ratio' 4096 '1' 'manufacturer_published' 'reviewed' @($b) $null),
  (Unk $v 'board_mass' 'Board mass' 'mass' 'g')) `
  @(@{id='angle_sensor_ideal';parameters=@('resolution_counts');curves=@();scope='As AS5600; board adds Grove I2C connector only.'}) @() 'Native Eagle board only; no solid.' $null)

# 9 Omron D2F --------------------------------------------------------------------------------------------
foreach($d in @(@{pn='D2F-01F';v='D2F-01F pin plunger, low operating force'},@{pn='D2F-01FL';v='D2F-01FL hinge lever, low operating force'})){
  $v=$d.v;$b='d2f-ds'
  Add (Rec ('switch-omron-'+$d.pn.ToLower()) ("Omron "+$d.pn+' subminiature basic switch') 'Omron' $d.pn $v 'SPDT subminiature limit switch. Force/travel values are in scanned tables of the original and are not yet transcribed.' @($b) @(
    (Unk $v 'operating_force' 'Operating force (OF)' 'force' 'N'),(Unk $v 'release_force' 'Release force (RF)' 'force' 'N'),
    (Unk $v 'pretravel' 'Pretravel (PT)' 'length' 'mm'),(Unk $v 'overtravel' 'Overtravel (OT)' 'length' 'mm'),(Unk $v 'movement_differential' 'Movement differential (MD)' 'length' 'mm'),
    (Claim $v 'contact_form' 'Contact form' 'text' 'SPDT (COM/NC/NO)' $null 'manufacturer_published' 'reviewed' @($b) $null)) `
    @(@{id='spdt_travel_hysteresis';parameters=@('contact_form');curves=@();scope='Ideal SPDT with travel hysteresis; thresholds must be filled from the original tables (currently unknown). Bounce unmodeled.'}) @() 'Dimensioned PDF drawing only; no native CAD (account-gated).' $null)
}
# 10 BMI270 ---------------------------------------------------------------------------------------------
$v='BMI270 sales part 0 273 017 008';$b='bmi270-ds'
Add (Rec 'sensor-bosch-bmi270' 'Bosch Sensortec BMI270 6-axis IMU' 'Bosch Sensortec' 'BMI270' $v 'Bare LGA IMU. Configuration blob/motion algorithms are proprietary and not modeled.' @($b) @(
  (Claim $v 'accel_ranges' 'Accelerometer full-scale ranges' 'text' '+/-2, 4, 8, 16 g' $null 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Recalled; confirm against rev 1.6'})),
  (Claim $v 'gyro_ranges' 'Gyroscope full-scale ranges' 'text' '+/-125, 250, 500, 1000, 2000 dps' $null 'manufacturer_published' 'unverified' @($b) ([ordered]@{note='Recalled; confirm against rev 1.6'})),
  (Unk $v 'accel_noise_density' 'Accelerometer noise density (ug/rtHz)' 'ratio' '1'),(Unk $v 'gyro_noise_density' 'Gyroscope noise density (dps/rtHz)' 'ratio' '1')) `
  @(@{id='imu_sampled';parameters=@('accel_ranges','gyro_ranges');curves=@();scope='Sampled 6-axis output with range, quantization, ODR and noise to be calibrated from datasheet tables.'}) @() 'Package/PCB reference only.' $null)
$v='SparkFun SEN-22301 Qwiic 6DoF BMI270'
Add (Rec 'module-sparkfun-sen-22301' 'SparkFun Qwiic 6DoF BMI270 breakout (SEN-22301)' 'SparkFun' 'SEN-22301' $v 'Breakout board; Eagle sources and driver snapshot acquired, no solid.' @($b) @(
  (Claim $v 'interface' 'Interface' 'text' 'I2C via Qwiic connector' $null 'manufacturer_published' 'unverified' @($b) $null)) `
  @(@{id='imu_sampled';parameters=@('interface');curves=@();scope='Board adds Qwiic I2C/power wiring only; sensor behavior per BMI270 record.'}) @() 'Native Eagle board only.' $null)
# 11 VL53L1X ---------------------------------------------------------------------------------------------
$v='VL53L1CXV0FY/1';$b='vl53-ds'
Add (Rec 'sensor-st-vl53l1x' 'STMicroelectronics VL53L1X time-of-flight sensor' 'STMicroelectronics' 'VL53L1CXV0FY/1' $v 'Bare module; Pololu #3415 carrier is separate.' @($b) @(
  (Claim $v 'package_length' 'Package length' 'length' 4.9 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'package_width' 'Package width' 'length' 2.5 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'package_height' 'Package height' 'length' 1.56 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'max_range' 'Maximum range' 'length' 4 'm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Conditional on mode, reflectance and ambient light'})),
  (Unk $v 'cover_window_offset' 'Cover-window calibration offset' 'length' 'mm')) `
  @(@{id='tof_ideal_ranging';parameters=@('max_range');curves=@();scope='Ideal distance=c*t/2 with no-target/out-of-range status and timing-budget sampling; reflectance/ambient/crosstalk errors need calibrated scenes.'}) @() 'Package/optical drawing in datasheet.' $null)
$v='Pololu #3415 VL53L1X carrier'
Add (Rec 'module-pololu-3415' 'Pololu 3415 VL53L1X time-of-flight distance sensor carrier' 'Pololu' '3415' $v 'Carrier with regulator and level shifting.' @($b,'vl53-carrier-step') @(
  (Claim $v 'max_range' 'Maximum range' 'length' 4 'm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='VL53L1X capability; conditional'}))) `
  @(@{id='tof_ideal_ranging';parameters=@('max_range');curves=@();scope='As sensor record; carrier STEP bundled.'}) @() 'Original carrier STEP; attach with Attach CAD.' $null)
# 12 EZO bearings ----------------------------------------------------------------------------------------
foreach($d in @(@{n='623ZZ';id=3;od=10;w=4;cr=633;c0=218;m=1.57;g=66000;o=78000},@{n='625ZZ';id=5;od=16;w=5;cr=1730;c0=670;m=4.53;g=44000;o=52000})){
  $v=$d.n+' SUJ2 unflanged shielded';$b='ezo-cat'
  Add (Rec ('bearing-ezo-'+$d.n.ToLower()) ("EZO $($d.n) deep-groove ball bearing") 'EZO (Sapporo Precision)' $d.n $v 'Shielded SUJ2 miniature ball bearing. Grease friction/stiffness uncalibrated.' @($b) @(
    (Claim $v 'bore' 'Bore diameter' 'length' $d.id 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'outer_diameter' 'Outer diameter' 'length' $d.od 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'width' 'Width' 'length' $d.w 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'dynamic_load_rating' 'Basic dynamic radial load rating Cr' 'force' $d.cr 'N' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'static_load_rating' 'Basic static radial load rating C0r' 'force' $d.c0 'N' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'mass' 'Reference mass' 'mass' $d.m 'g' 'manufacturer_published' 'reviewed' @($b) $null),
    (Claim $v 'limit_speed_grease' 'Limiting speed, grease' 'frequency' ($d.g/60) 'Hz' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{original="$($d.g) min^-1"})),
    (Claim $v 'limit_speed_oil' 'Limiting speed, oil' 'frequency' ($d.o/60) 'Hz' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{original="$($d.o) min^-1"})),
    (Unk $v 'start_torque' 'Starting friction torque' 'torque' 'N m')) `
    @(@{id='bearing_load_life';parameters=@('dynamic_load_rating','static_load_rating','limit_speed_grease');curves=@();scope='Constrained rotation with load/speed limit checks and L10=(Cr/P)^3 life (needs equivalent load); no calibrated friction. EZO generic mu=0.0013 is an oiled low-load value, not a ZZ-grease drag model.'}) @() 'Catalog envelope table; reconstruct revolved ring/ball/shield body.' $null)
}
# 13 Wheel and hub ----------------------------------------------------------------------------------------
$v='#1088 32x7 mm wheel, white';$b='wheel-step'
Add (Rec 'wheel-pololu-1088' 'Pololu 1088 Wheel 32x7 mm (white, pair)' 'Pololu' '1088' $v 'Press-fit 3 mm D-shaft silicone-tire wheel. Black STEP in the archive is another colour variant.' @($b) @(
  (Claim $v 'diameter' 'Nominal diameter' 'length' 32 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'width' 'Width' 'length' 7 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'bore_shaft' 'Bore shaft' 'text' '3 mm D-shaft press fit' $null 'manufacturer_published' 'reviewed' @($b) $null),
  (Unk $v 'mass' 'Mass' 'mass' 'g'),(Unk $v 'friction_coefficient' 'Tire friction coefficient' 'ratio' '1'),(Unk $v 'rolling_resistance' 'Rolling resistance coefficient' 'ratio' '1')) `
  @(@{id='wheel_rolling_ideal';parameters=@('diameter','width');curves=@();scope='Ideal no-slip rolling: v=w*r with nominal r=16 mm (circumference 100.531 mm); slip/loaded radius unknown.'}) @() 'Original white-wheel STEP; attach with Attach CAD.' $null)
$v='#1078 aluminum hub, 3 mm shaft, #4-40'
Add (Rec 'hub-pololu-1078' 'Pololu 1078 Universal Aluminum Mounting Hub (3 mm shaft, #4-40)' 'Pololu' '1078' $v 'Shaft-compatible hub; not a proven direct mate to wheel #1088.' @('hub-step','hub-dwg') @(
  (Claim $v 'shaft_bore' 'Shaft bore' 'length' 3 'mm' 'manufacturer_published' 'reviewed' @('hub-dwg') ([ordered]@{note='Product designation'})),
  (Claim $v 'mount_thread' 'Mounting thread' 'text' '#4-40' $null 'manufacturer_published' 'reviewed' @('hub-dwg') $null)) `
  @(@{id='rigid_hub';parameters=@('shaft_bore');curves=@();scope='Rigid shaft coupling; no compliance.'}) @() 'Original hub STEP; attach with Attach CAD.' $null)
# 14 T8 lead screw ----------------------------------------------------------------------------------------
$v='LSN-S-MB-320 screw with black nut';$b='t8-ds'
Add (Rec 'leadscrew-stepperonline-lsn-s-mb-320' 'STEPPERONLINE LSN-S-MB-320 T8 lead screw and nut' 'STEPPERONLINE' 'LSN-S-MB-320' $v 'Tr8 lead screw (320 mm) with black flange nut. Pitch/starts and efficiency unpublished; none assumed.' @($b) @(
  (Claim $v 'major_diameter' 'Screw major diameter' 'length' 8 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'screw_length' 'Screw length' 'length' 320 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'lead' 'Lead per revolution' 'length' 8 'mm' 'manufacturer_published' 'reviewed' @($b) ([ordered]@{note='Per revolution; pitch and starts not stated'})),
  (Claim $v 'nut_flange_diameter' 'Nut flange diameter' 'length' 25.4 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'nut_hole_pcd' 'Nut hole PCD' 'length' 19.05 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'nut_hole_diameter' 'Nut mounting hole diameter (x3)' 'length' 3.56 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'nut_length' 'Nut length' 'length' 19.05 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Claim $v 'nut_flange_thickness' 'Nut flange thickness' 'length' 3.81 'mm' 'manufacturer_published' 'reviewed' @($b) $null),
  (Unk $v 'efficiency' 'Drive efficiency' 'ratio' '%'),(Unk $v 'backlash' 'Backlash' 'length' 'mm'),(Unk $v 'pitch' 'Thread pitch' 'length' 'mm')) `
  @(@{id='leadscrew_ideal';parameters=@('lead');curves=@();scope='Ideal kinematics x=lead*theta/2pi (100 rpm -> 13.333 mm/s). Load/torque/efficiency not modeled.'}) @() 'Drawing reconstruction (flange nut dimensions in datasheet); helical thread detail incomplete.' $null)

# Copy originals and write the shared library -------------------------------------------------------------
foreach($s in $src.Values){ if($s.local){ foreach($dir in "$CadRes\manufacturer-foundation","$DewbAssets\manufacturer-foundation"){
  $t=Join-Path $dir $s.originalFilename; if(-not (Test-Path $t)){ Copy-Item $s.local $t } } } }
$used=@{}; foreach($rr in $Recs){ foreach($x in $rr.engineering.specification.sources){ $used[$x.id]=1 } }
$lib=[ordered]@{format='djehuti-component-library';schemaVersion=1;records=@($Recs)}
$json=($lib|ConvertTo-Json -Depth 20)
$enc=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText("$CadRes\robotics-component-library.json",$json,$enc)
[IO.File]::WriteAllText("$DewbAssets\robotics-component-library.json",$json,$enc)
"records=$($Recs.Count) sources=$($used.Count)"
