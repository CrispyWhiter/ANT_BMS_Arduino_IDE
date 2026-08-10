#include "config_web_portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <ctype.h>
#include <math.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "../app_config.h"
#include "../core/runtime_profile.h"
#include "../core/diagnostic_log.h"
#include "../core/runtime_settings.h"
#include "../core/time_utils.h"
#include "bms_ble_service.h"

namespace ConfigWebPortal {
namespace {

WebServer server(AppConfig::WebConfig::HttpPort);
DNSServer dnsServer;

bool active = false;
bool accessObserved = false;
uint32_t rebootAt = 0;
uint32_t startedAt = 0;
char apName[33] = {};
String sessionToken;
IPAddress sessionClientIp;
bool sessionClientBound = false;
bool routesConfigured = false;
bool sessionAdministrator = false;
bool keepAliveWhileActive = false;

constexpr char kCookieName[] = "BMS_SESSION";
constexpr uint8_t kAdministratorPasswordHash[32] = {
    0x15, 0xE2, 0xB0, 0xD3, 0xC3, 0x38, 0x91, 0xEB,
    0xB0, 0xF1, 0xEF, 0x60, 0x9E, 0xC4, 0x19, 0x42,
    0x0C, 0x20, 0xE3, 0x20, 0xCE, 0x94, 0xC6, 0x5F,
    0xBC, 0x8C, 0x33, 0x12, 0x44, 0x8E, 0xB2, 0x25};

constexpr char kLoginPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>BMS 管理登录</title><style>
:root{--bg:#30373c;--panel:#202a32;--panel2:#26323a;--blue:#2f84ff;--text:#f5f7f9;--muted:#aeb8c1;--line:rgba(255,255,255,.08);--shadow:0 18px 48px rgba(0,0,0,.28)}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",Arial,sans-serif;color:var(--text);background:var(--bg)}
body{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:18px;background:radial-gradient(circle at 28% 12%,#4b565d 0,#384147 34%,#2b3339 72%,#252d33 100%)}
.login{width:min(360px,100%);padding:17px;border:1px solid var(--line);border-radius:19px;background:linear-gradient(145deg,rgba(35,47,57,.98),rgba(26,36,44,.98));box-shadow:var(--shadow)}
.logo{display:flex;align-items:center;gap:10px;margin-bottom:13px}.mark{width:40px;height:40px;border-radius:12px;background:linear-gradient(180deg,#4697ff,#2f84ff 55%,#2777e6);display:grid;place-items:center;font-size:20px;font-weight:900;box-shadow:inset 0 1px rgba(255,255,255,.3),inset 0 -2px rgba(13,75,161,.28),0 5px 10px rgba(47,132,255,.2)}
.brand{font-size:17px;font-weight:850;letter-spacing:.2px}.sub{margin-top:2px;color:var(--muted);font-size:11px}.label{display:block;margin:0 0 5px;font-size:12px;font-weight:750;color:#dce4ea}
.input{width:100%;height:39px;border:1px solid rgba(255,255,255,.12);border-radius:10px;padding:0 11px;background:#f5f8fa;color:#1f2b33;font-size:15px;outline:none;box-shadow:inset 0 1px 3px rgba(0,0,0,.08)}
.input:focus{border-color:#76afff;box-shadow:0 0 0 2px rgba(47,132,255,.16),inset 0 1px 3px rgba(0,0,0,.08)}.btn{width:100%;height:39px;margin-top:9px;border:1px solid rgba(255,255,255,.09);border-radius:10px;background:linear-gradient(180deg,#4697ff 0,#3188ff 50%,#2777e6 100%);color:#fff;font-size:13px;font-weight:850;box-shadow:inset 0 1px rgba(255,255,255,.3),inset 0 -2px rgba(13,75,161,.28),0 5px 11px rgba(47,132,255,.2);text-shadow:0 1px rgba(0,0,0,.14)}.btn:active{transform:translateY(1px);box-shadow:inset 0 2px 4px rgba(0,0,0,.18),0 1px 3px rgba(0,0,0,.18)}
.meta{display:flex;justify-content:space-between;gap:10px;margin-top:10px;color:#8f9ca6;font-size:10px}.meta b{color:#cfd7dd;font-weight:700}
</style></head><body><main class="login"><div class="logo"><div class="mark">B</div><div><div class="brand">无用脑洞研究所</div><div class="sub">BMS 配置中心 · v1.0.0</div></div></div><form action="/login" method="post"><label class="label" for="password">管理密码</label><input id="password" class="input" name="password" type="password" maxlength="16" autocomplete="current-password" placeholder="请输入 6-16 位密码" required><button class="btn" type="submit">进入设置</button></form><div class="meta"><span>设备本机管理</span><span>192.168.4.1</span></div></main></body></html>
)HTML";

constexpr char kLoginFailedPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><title>登录失败</title><style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:18px;background:radial-gradient(circle at 28% 12%,#4b565d 0,#343d43 42%,#273037 100%);color:#f5f7f9;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",Arial,sans-serif}.box{width:min(360px,100%);padding:17px;border:1px solid rgba(255,255,255,.08);border-radius:19px;background:linear-gradient(145deg,#25313a,#1e2931);box-shadow:0 18px 48px rgba(0,0,0,.28)}h1{margin:0 0 6px;font-size:18px}.bad{color:#ffb5b1;margin:0 0 12px;font-size:12px}a{display:block;height:39px;line-height:39px;border:1px solid rgba(255,255,255,.09);border-radius:10px;background:linear-gradient(180deg,#4697ff,#3188ff 52%,#2777e6);box-shadow:inset 0 1px rgba(255,255,255,.3),inset 0 -2px rgba(13,75,161,.28),0 5px 11px rgba(47,132,255,.2);color:#fff;text-align:center;text-decoration:none;font-size:13px;font-weight:850}
</style></head><body><main class="box"><h1>密码错误</h1><p class="bad">请重新输入管理密码。</p><a href="/">返回登录</a></main></body></html>
)HTML";

constexpr char kConfigPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>BMS 设置</title><style>
:root{--bg:#30383e;--panel:#222d35;--panel2:#283640;--panel3:#1c262e;--popup:#3c474f;--popup2:#46525b;--blue:#3188ff;--blue2:#2474e8;--text:#f7f9fb;--muted:#aeb9c1;--soft:#87959f;--line:rgba(255,255,255,.085);--ok:#74e0aa;--bad:#ff9d96}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}html,body{margin:0;min-height:100%;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",Arial,sans-serif;color:var(--text);background:var(--bg)}body{background:radial-gradient(circle at 22% 5%,#4a555c 0,#394248 28%,#30383e 58%,#293238 100%)}button,input{font:inherit}.wrap{width:100%;max-width:620px;margin:auto;padding:6px 7px 12px}.top{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:3px 3px 7px}.brand{font-size:17px;font-weight:850}.sub{margin-top:1px;color:var(--muted);font-size:10px}.version{padding:4px 7px;border:1px solid rgba(255,255,255,.06);border-radius:9px;background:linear-gradient(180deg,#46525a,#354149);box-shadow:inset 0 1px rgba(255,255,255,.1),0 2px 5px rgba(0,0,0,.18);color:#edf2f5;font-size:10px;font-weight:800}
.card{margin:0 0 6px;padding:9px;border:1px solid var(--line);border-radius:15px;background:linear-gradient(145deg,rgba(38,50,59,.98),rgba(29,40,48,.98));box-shadow:inset 0 1px rgba(255,255,255,.025),0 6px 15px rgba(0,0,0,.13)}.section-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:6px}.title{margin:0;font-size:14px;line-height:1.2}.badge{max-width:59%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--muted);font-size:9.5px}
.type-grid{display:grid;grid-template-columns:1fr 1fr;gap:5px}.type-option{position:relative}.type-option input{position:absolute;opacity:0;pointer-events:none}.type-option span,.btn,.picker,.device{transition:transform .08s ease,box-shadow .08s ease,filter .08s ease}.type-option span{display:flex;align-items:center;justify-content:center;min-height:34px;border:1px solid rgba(255,255,255,.07);border-radius:10px;background:linear-gradient(180deg,#26343e,#1d2931);box-shadow:inset 0 1px rgba(255,255,255,.07),inset 0 -1px rgba(0,0,0,.22),0 3px 7px rgba(0,0,0,.15);color:#e5ebef;font-size:12px;font-weight:800}.type-option input:checked+span{border-color:#579dff;background:linear-gradient(180deg,#4594ff 0,#2f84ff 52%,#2879e9 100%);box-shadow:inset 0 1px rgba(255,255,255,.28),inset 0 -2px rgba(12,78,171,.28),0 4px 9px rgba(28,110,225,.24);color:#fff}.type-option:active span{transform:translateY(1px);box-shadow:inset 0 1px rgba(0,0,0,.16),0 1px 3px rgba(0,0,0,.14)}
.btn{height:34px;border:1px solid rgba(255,255,255,.08);border-radius:10px;padding:0 11px;background:linear-gradient(180deg,#4697ff 0,#3188ff 48%,#2777e6 100%);box-shadow:inset 0 1px rgba(255,255,255,.3),inset 0 -2px rgba(13,75,161,.28),0 4px 9px rgba(21,92,188,.22);color:#fff;font-size:12px;font-weight:850;text-shadow:0 1px rgba(0,0,0,.15)}.btn:active:not(:disabled){transform:translateY(1px);box-shadow:inset 0 2px 4px rgba(0,0,0,.18),0 1px 3px rgba(0,0,0,.18)}.btn:disabled{opacity:.42;filter:saturate(.55)}.btn.secondary{background:linear-gradient(180deg,#344550,#263641 55%,#22313a 100%);box-shadow:inset 0 1px rgba(255,255,255,.1),inset 0 -2px rgba(0,0,0,.18),0 3px 7px rgba(0,0,0,.15);color:#edf2f5}.btn.full{width:100%}.btn.compact{width:100%;margin-top:5px}
.field-line{display:grid;grid-template-columns:auto minmax(80px,1fr) auto;align-items:center;gap:6px;margin-bottom:5px;padding:5px 7px;border:1px solid rgba(255,255,255,.035);border-radius:10px;background:#1d2830}.field-line label{font-size:11px;font-weight:800;color:#e0e6ea}.input{width:100%;height:30px;border:1px solid rgba(255,255,255,.1);border-radius:8px;padding:0 8px;background:#eef3f6;color:#1f2b33;font-size:13px;font-weight:700;outline:none;box-shadow:inset 0 1px 3px rgba(0,0,0,.08)}.input:focus{border-color:#6aa8ff;box-shadow:0 0 0 2px rgba(49,136,255,.14),inset 0 1px 3px rgba(0,0,0,.08)}.unit{color:var(--muted);font-size:10px}
.device-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px;max-height:105px;overflow:auto;padding-right:1px}.device{width:100%;min-width:0;padding:7px 8px;text-align:left;border:1px solid rgba(255,255,255,.07);border-radius:10px;background:linear-gradient(180deg,#24313a,#1c272f);box-shadow:inset 0 1px rgba(255,255,255,.05),0 2px 5px rgba(0,0,0,.13);color:var(--text)}.device:active{transform:translateY(1px)}.device.selected{border-color:#62a5ff;background:linear-gradient(180deg,#31516b,#294357);box-shadow:inset 0 1px rgba(255,255,255,.08),0 0 0 1px rgba(49,136,255,.08),0 3px 7px rgba(0,0,0,.14)}.device-name{font-size:11.5px;font-weight:850;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.device-meta{margin-top:2px;color:#91a1ac;font-size:9px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.empty,.status{padding:6px 8px;border-radius:9px;background:#1b252d;color:#b0bbc3;font-size:10px;line-height:1.35}.empty{text-align:center;border:1px dashed rgba(255,255,255,.09);grid-column:1/-1}.status{margin-top:5px}.status.ok{color:#d3ffe9;border:1px solid rgba(116,224,170,.3)}.status.bad{color:#ffd5d2;border:1px solid rgba(255,157,150,.32)}
.settings-row{display:grid;grid-template-columns:1.2fr .8fr;gap:5px;align-items:stretch}.switch-box,.select-box{min-height:40px;padding:5px 7px;border:1px solid rgba(255,255,255,.035);border-radius:10px;background:#1d2830}.switch-box{display:flex;align-items:center;justify-content:space-between;gap:6px}.switch-title{font-size:11px;font-weight:800}.switch-hint{margin-top:1px;color:#8998a3;font-size:8.5px}.switch{width:39px;height:22px;accent-color:var(--blue)}.select-box{display:grid;grid-template-columns:auto 1fr;align-items:center;gap:6px}.select-box label{color:#9dabb5;font-size:9px;white-space:nowrap}.picker{width:100%;height:29px;border:1px solid rgba(255,255,255,.1);border-radius:8px;padding:0 7px;background:linear-gradient(180deg,#354650,#273741);box-shadow:inset 0 1px rgba(255,255,255,.1),inset 0 -1px rgba(0,0,0,.22),0 2px 4px rgba(0,0,0,.13);color:#fff;font-size:10.5px;font-weight:800}.picker:active{transform:translateY(1px);box-shadow:inset 0 2px 3px rgba(0,0,0,.18)}
.advanced summary{cursor:pointer;list-style:none;font-size:11px;font-weight:800;color:#dce3e8}.advanced summary::-webkit-details-marker{display:none}.advanced summary:after{content:'＋';float:right;color:#95a2ab}.advanced[open] summary:after{content:'－'}.inside{padding-top:6px}.password-row{display:grid;grid-template-columns:1fr 98px;gap:5px}.footer{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:0 3px;color:#88969f;font-size:9.5px}.link{color:#bdd9ff;text-decoration:none}
.modal{position:fixed;inset:0;z-index:30;display:none;align-items:center;justify-content:center;padding:18px;background:rgba(10,15,19,.45);backdrop-filter:blur(2px);-webkit-backdrop-filter:blur(2px)}.modal.show{display:flex}.modal-panel{width:min(300px,86vw);padding:10px;border:1px solid rgba(255,255,255,.11);border-radius:16px;background:linear-gradient(145deg,#414d55,#374249);box-shadow:inset 0 1px rgba(255,255,255,.08),0 18px 40px rgba(0,0,0,.34)}.modal-title{margin:0 0 7px;font-size:13px;font-weight:850;color:#fff}.minute-grid{display:grid;grid-template-columns:1fr 1fr;gap:6px}.minute-option{height:34px;border:1px solid rgba(255,255,255,.09);border-radius:10px;background:linear-gradient(180deg,#4b5962,#414d55);box-shadow:inset 0 1px rgba(255,255,255,.09),inset 0 -2px rgba(0,0,0,.16),0 2px 5px rgba(0,0,0,.15);color:#fff;font-size:11.5px;font-weight:800}.minute-option:active{transform:translateY(1px);box-shadow:inset 0 2px 4px rgba(0,0,0,.2)}.minute-option.selected{border-color:#70adff;background:linear-gradient(180deg,#4b9cff,#3188ff 55%,#2877e3);box-shadow:inset 0 1px rgba(255,255,255,.28),inset 0 -2px rgba(14,77,164,.24),0 4px 9px rgba(29,105,205,.22)}
@media(max-width:390px){.wrap{padding:5px 5px 10px}.card{padding:8px;border-radius:14px}.settings-row{grid-template-columns:1.15fr .85fr}.device-list{grid-template-columns:1fr 1fr}.password-row{grid-template-columns:1fr 88px}.btn{padding:0 8px}}
</style></head><body><main class="wrap">
<header class="top"><div><div class="brand">无用脑洞研究所</div><div class="sub">保护板配置中心</div></div><span class="version">v1.0.0</span></header>
<section class="card"><div class="section-head"><h2 class="title">保护板品牌</h2><span id="scanBadge" class="badge">读取中…</span></div><div class="type-grid"><label class="type-option"><input id="typeAnt" name="bmsType" type="radio" value="ant"><span>蚂蚁 ANT</span></label><label class="type-option"><input id="typeJikong" name="bmsType" type="radio" value="jikong"><span>极空 JK</span></label><label class="type-option"><input id="typeJiabaida" name="bmsType" type="radio" value="jiabaida"><span>嘉佰达 JBD</span></label><label class="type-option"><input id="typeYanyang" name="bmsType" type="radio" value="yanyang"><span>彦阳 YY</span></label></div><button id="rescanButton" class="btn secondary compact" type="button">按当前品牌重新扫描</button></section>
<section class="card"><div class="section-head"><h2 id="deviceTitle" class="title">保护板设备</h2><span id="deviceDescription" class="badge">读取扫描结果…</span></div><div class="field-line"><label for="coefficient">里程系数</label><input id="coefficient" class="input" type="number" min="0.01" max="100" step="0.001" inputmode="decimal"><span class="unit">km/Ah</span></div><div id="devices" class="device-list"><div class="empty">正在读取扫描结果…</div></div><div id="selectedStatus" class="status">当前品牌没有可选设备</div><button id="saveButton" class="btn full compact" type="button" disabled>保存并连接保护板</button><div id="saveStatus" class="status">正在读取当前配置…</div></section>
<section class="card"><div class="section-head"><h2 class="title">屏幕自动休眠</h2><span id="displayStatus" class="badge">读取中…</span></div><div class="settings-row"><div class="switch-box"><div><div class="switch-title">停车自动休眠</div><div class="switch-hint">三击可手动开关屏幕</div></div><input id="autoDisplayEnabled" class="switch" type="checkbox"></div><div class="select-box"><label>休眠时间</label><button id="sleepPicker" class="picker" type="button">5 分钟 ▾</button></div></div><button id="displayButton" class="btn secondary compact" type="button">保存屏幕设置</button></section>
<!--PRIVILEGED_SECTION--><section class="card"><details class="advanced"><summary>修改管理密码</summary><div class="inside"><div class="password-row"><input id="newPassword" class="input" type="password" maxlength="16" placeholder="6-16 位字母或数字"><button id="passwordButton" class="btn secondary" type="button">保存密码</button></div><div id="passwordStatus" class="status">密码仅保存在设备本机。</div></div></details></section>
<div class="footer"><span id="apInfo">热点：无用脑洞研究所</span><a class="link" href="/logout">退出登录</a></div></main>
<div id="sleepModal" class="modal" role="dialog" aria-modal="true" aria-label="选择休眠时间"><div class="modal-panel"><div class="modal-title">选择休眠时间</div><div class="minute-grid"><button class="minute-option" type="button" data-minutes="5">5 分钟</button><button class="minute-option" type="button" data-minutes="6">6 分钟</button><button class="minute-option" type="button" data-minutes="7">7 分钟</button><button class="minute-option" type="button" data-minutes="8">8 分钟</button><button class="minute-option" type="button" data-minutes="9">9 分钟</button><button class="minute-option" type="button" data-minutes="10">10 分钟</button></div></div></div>
<script>(function(){'use strict';
var selectedAddress='',selectedName='',selectedType='ant',scannedType='ant',pairedType='ant',coefficientEdited=false,sleepValue=5;
function id(v){return document.getElementById(v)}function trim(v){return String(v||'').replace(/^\s+|\s+$/g,'')}function typeName(v){if(v==='jikong')return'极空 JK';if(v==='jiabaida')return'嘉佰达 JBD';if(v==='yanyang')return'彦阳 YY';return'蚂蚁 ANT'}function form(o){var a=[],k;for(k in o)if(o.hasOwnProperty(k))a.push(encodeURIComponent(k)+'='+encodeURIComponent(o[k]));return a.join('&')}function setStatus(el,msg,state){el.textContent=msg;el.className='status'+(state?' '+state:'')}function request(method,path,body,cb){var x=new XMLHttpRequest();x.open(method,path,true);x.timeout=12000;if(method==='POST')x.setRequestHeader('Content-Type','application/x-www-form-urlencoded;charset=UTF-8');x.onreadystatechange=function(){if(x.readyState!==4)return;var r={};try{r=JSON.parse(x.responseText||'{}')}catch(e){r={message:'设备返回内容异常'}}cb(x.status>=200&&x.status<300,r)};x.onerror=function(){cb(false,{message:'热点连接已中断'})};x.ontimeout=function(){cb(false,{message:'请求超时'})};x.send(body||null)}
function clearSelection(msg){selectedAddress='';selectedName='';var b=id('devices').getElementsByTagName('button'),i;for(i=0;i<b.length;i++)b[i].className='device';setStatus(id('selectedStatus'),msg||'尚未选择设备','');id('saveButton').disabled=true}
function selectDevice(a,n,b){selectedAddress=a||'';selectedName=n||'';var all=id('devices').getElementsByTagName('button'),i;for(i=0;i<all.length;i++)all[i].className='device';if(b)b.className='device selected';setStatus(id('selectedStatus'),'已选择：'+selectedName+' · '+selectedAddress,'ok');id('saveButton').disabled=false}
function updateType(){id('typeAnt').checked=selectedType==='ant';id('typeJikong').checked=selectedType==='jikong';id('typeJiabaida').checked=selectedType==='jiabaida';id('typeYanyang').checked=selectedType==='yanyang';id('deviceTitle').textContent=typeName(selectedType)+' 设备';id('rescanButton').textContent='重新扫描 '+typeName(selectedType);id('scanBadge').textContent='扫描缓存：'+typeName(scannedType);if(selectedType!==scannedType){id('deviceDescription').textContent='需要重新扫描';id('devices').innerHTML='<div class="empty">品牌已切换，请重新扫描</div>';clearSelection('当前品牌没有可选设备')}else id('deviceDescription').textContent='本次开机扫描结果'}
function setSleepMinutes(v){var n=parseInt(v,10);if(!(n>=5&&n<=10))n=5;sleepValue=n;id('sleepPicker').textContent=n+' 分钟 ▾';var a=document.getElementsByClassName('minute-option'),i;for(i=0;i<a.length;i++)a[i].className='minute-option'+(parseInt(a[i].getAttribute('data-minutes'),10)===n?' selected':'')}
function openSleepPicker(){id('sleepModal').className='modal show'}function closeSleepPicker(){id('sleepModal').className='modal'}
function loadDevices(){request('GET','/api/devices',null,function(ok,r){var c=id('devices');if(!ok){c.innerHTML='<div class="empty">扫描结果读取失败</div>';clearSelection('当前品牌没有可选设备');return}scannedType=r.scanType||scannedType;if(selectedType!==scannedType){updateType();return}c.innerHTML='';var list=r.devices||[];if(!list.length){c.innerHTML='<div class="empty">未扫描到 '+typeName(scannedType)+' 设备</div>';clearSelection('当前品牌没有可选设备');return}clearSelection('请选择一个保护板设备');list.forEach(function(d){var b=document.createElement('button');b.type='button';b.className='device';b.innerHTML='<div class="device-name"></div><div class="device-meta"></div>';b.children[0].textContent=d.name||'未命名设备';b.children[1].textContent=(d.address||'未知地址')+' · '+d.rssi+' dBm';b.onclick=function(){selectDevice(d.address,d.name||'未命名设备',b)};c.appendChild(b);if(pairedType===scannedType&&(d.address||'').toLowerCase()===(r.pairedAddress||'').toLowerCase())selectDevice(d.address,d.name||'未命名设备',b)})})}
function loadStatus(){request('GET','/api/status',null,function(ok,r){if(!ok){setStatus(id('saveStatus'),r.message||'配置读取失败','bad');return}pairedType=r.pairedType||'ant';scannedType=r.scanType||pairedType;selectedType=scannedType;if(!coefficientEdited)id('coefficient').value=Number(r.rangeCoefficient||0).toFixed(3);id('autoDisplayEnabled').checked=!!r.autoDisplayEnabled;setSleepMinutes(r.sleepMinutes||5);id('displayStatus').textContent=(r.autoDisplayEnabled?'已启用 · ':'已关闭 · ')+(r.sleepMinutes||5)+' 分钟';id('apInfo').textContent='热点：'+(r.apName||'无用脑洞研究所');setStatus(id('saveStatus'),r.pairedAddress?'当前配对：'+typeName(pairedType)+' · '+r.pairedAddress:'尚未保存保护板','');updateType();loadDevices()})}
function chooseType(v){selectedType=v;updateType()}
function rescan(){id('rescanButton').disabled=true;request('POST','/api/rescan',form({type:selectedType}),function(ok,r){if(!ok){id('rescanButton').disabled=false;setStatus(id('saveStatus'),r.message||'重新扫描请求失败','bad');return}document.body.innerHTML='<main class="wrap"><section class="card"><h2 class="title">设备正在重启并扫描 '+typeName(selectedType)+'</h2><div class="status">完成后重新连接“无用脑洞研究所”热点。</div></section></main>'})}
function saveMain(){var coef=trim(id('coefficient').value),num=parseFloat(coef);if(selectedType!==scannedType){setStatus(id('saveStatus'),'请先重新扫描所选品牌','bad');return}if(!selectedAddress){setStatus(id('saveStatus'),'请先选择保护板','bad');return}if(!(num>=0.01&&num<=100)){setStatus(id('saveStatus'),'里程系数必须在 0.01-100 之间','bad');return}id('saveButton').disabled=true;request('POST','/api/save',form({type:selectedType,address:selectedAddress,name:selectedName,coefficient:coef}),function(ok,r){if(!ok){id('saveButton').disabled=false;setStatus(id('saveStatus'),r.message||'保存失败','bad');return}setStatus(id('saveStatus'),'保存成功，设备正在重启','ok')})}
function saveDisplay(){var sleep=sleepValue,enabled=id('autoDisplayEnabled').checked?1:0;if(!(sleep>=5&&sleep<=10)){id('displayStatus').textContent='休眠时间无效';return}id('displayButton').disabled=true;request('POST','/api/display',form({enabled:enabled,sleepMinutes:sleep}),function(ok,r){id('displayButton').disabled=false;id('displayStatus').textContent=ok?'已保存 · '+sleep+' 分钟':(r.message||'保存失败')})}
/*PRIVILEGED_SCRIPT*/
function savePassword(){var p=id('newPassword').value;id('passwordButton').disabled=true;request('POST','/api/password',form({password:p}),function(ok,r){id('passwordButton').disabled=false;if(ok){id('newPassword').value='';setStatus(id('passwordStatus'),'密码已保存','ok')}else setStatus(id('passwordStatus'),r.message||'保存失败','bad')})}
function init(){var mins=document.getElementsByClassName('minute-option'),i;id('typeAnt').onchange=function(){if(this.checked)chooseType('ant')};id('typeJikong').onchange=function(){if(this.checked)chooseType('jikong')};id('typeJiabaida').onchange=function(){if(this.checked)chooseType('jiabaida')};id('typeYanyang').onchange=function(){if(this.checked)chooseType('yanyang')};id('rescanButton').onclick=rescan;id('saveButton').onclick=saveMain;id('displayButton').onclick=saveDisplay;id('passwordButton').onclick=savePassword;id('sleepPicker').onclick=openSleepPicker;id('sleepModal').onclick=function(e){if(e.target===this)closeSleepPicker()};for(i=0;i<mins.length;i++)mins[i].onclick=function(){setSleepMinutes(this.getAttribute('data-minutes'));closeSleepPicker()};id('coefficient').oninput=function(){coefficientEdited=true};/*PRIVILEGED_INIT*/loadStatus()}if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();})();</script></body></html>
)HTML";

constexpr char kPrivilegedSection[] PROGMEM = R"HTML(
<section class="card"><div class="section-head"><h2 class="title">仪表盘测试模式</h2><span id="testModeStatus" class="badge">读取中…</span></div><div class="switch-box"><div><div class="switch-title">启用测试数据</div><div class="switch-hint">切换后设备将自动重启</div></div><input id="dashboardTestEnabled" class="switch" type="checkbox"></div><button id="testModeButton" class="btn secondary compact" type="button">保存测试设置</button></section>
)HTML";

constexpr char kPrivilegedScript[] PROGMEM = R"JS(
function loadDashboardTest(){request('GET','/api/test-mode',null,function(ok,r){if(!ok){id('testModeStatus').textContent=r.message||'读取失败';return}id('dashboardTestEnabled').checked=!!r.enabled;id('testModeStatus').textContent=r.enabled?'已启用':'已关闭'})}
function saveDashboardTest(){var enabled=id('dashboardTestEnabled').checked?1:0;id('testModeButton').disabled=true;request('POST','/api/test-mode',form({enabled:enabled}),function(ok,r){if(!ok){id('testModeButton').disabled=false;id('testModeStatus').textContent=r.message||'保存失败';return}document.body.innerHTML='<main class="wrap"><section class="card"><h2 class="title">测试设置已保存</h2><div class="status">设备正在重启，请稍后重新连接配置热点。</div></section></main>'})}
)JS";

constexpr char kPrivilegedInit[] PROGMEM =
    "id(\'testModeButton\').onclick=saveDashboardTest;loadDashboardTest();";

void noteAccess() {
  accessObserved = true;
}

void addCommonHeaders() {
  server.client().setNoDelay(true);
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Connection", "close");
  server.sendHeader("X-Content-Type-Options", "nosniff");
}

String jsonEscape(const char *value) {
  String result;
  if (value == nullptr) return result;
  result.reserve(strlen(value) + 8U);
  while (*value != '\0') {
    const char c = *value++;
    switch (c) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20U) result += c;
        break;
    }
  }
  return result;
}

void sendJson(int statusCode, const String &payload) {
  noteAccess();
  addCommonHeaders();
  server.send(statusCode, "application/json; charset=utf-8", payload);
}

void sendLoginPage(bool failed = false) {
  noteAccess();
  addCommonHeaders();
  if (failed) server.send_P(403, "text/html; charset=utf-8", kLoginFailedPage);
  else server.send_P(200, "text/html; charset=utf-8", kLoginPage);
}

void sendConfigPage() {
  noteAccess();
  addCommonHeaders();

  constexpr char kSectionMarker[] = "<!--PRIVILEGED_SECTION-->";
  constexpr char kScriptMarker[] = "/*PRIVILEGED_SCRIPT*/";
  constexpr char kInitMarker[] = "/*PRIVILEGED_INIT*/";

  const char *sectionMarker = strstr(kConfigPage, kSectionMarker);
  const char *scriptMarker = strstr(kConfigPage, kScriptMarker);
  const char *initMarker = strstr(kConfigPage, kInitMarker);
  if (sectionMarker == nullptr || scriptMarker == nullptr || initMarker == nullptr ||
      !(sectionMarker < scriptMarker && scriptMarker < initMarker)) {
    server.send(500, "text/plain; charset=utf-8", "配置页面生成失败");
    return;
  }

  const char *sectionAfter = sectionMarker + strlen(kSectionMarker);
  const char *scriptAfter = scriptMarker + strlen(kScriptMarker);
  const char *initAfter = initMarker + strlen(kInitMarker);

  size_t contentLength = strlen(kConfigPage) - strlen(kSectionMarker) -
      strlen(kScriptMarker) - strlen(kInitMarker);
  if (sessionAdministrator) {
    contentLength += strlen(kPrivilegedSection) + strlen(kPrivilegedScript) +
        strlen(kPrivilegedInit);
  }

  server.setContentLength(contentLength);
  server.send(200, "text/html; charset=utf-8", "");

  server.sendContent_P(kConfigPage,
                       static_cast<size_t>(sectionMarker - kConfigPage));
  if (sessionAdministrator) server.sendContent_P(kPrivilegedSection);
  server.sendContent_P(sectionAfter,
                       static_cast<size_t>(scriptMarker - sectionAfter));
  if (sessionAdministrator) server.sendContent_P(kPrivilegedScript);
  server.sendContent_P(scriptAfter,
                       static_cast<size_t>(initMarker - scriptAfter));
  if (sessionAdministrator) server.sendContent_P(kPrivilegedInit);
  server.sendContent_P(initAfter);
}

bool matchesAdministratorPassword(const char *candidate) {
  if (candidate == nullptr) return false;
  uint8_t digest[32] = {};
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  if (mbedtls_sha256(reinterpret_cast<const unsigned char *>(candidate),
                     strlen(candidate),
                     digest,
                     0) != 0) {
    return false;
  }
#else
  if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char *>(candidate),
                         strlen(candidate),
                         digest,
                         0) != 0) {
    return false;
  }
#endif

  uint8_t difference = 0U;
  for (size_t i = 0; i < sizeof(digest); ++i) {
    difference |= static_cast<uint8_t>(digest[i] ^ kAdministratorPasswordHash[i]);
  }
  return difference == 0U;
}

String requestSessionToken() {
  const String cookie = server.header("Cookie");
  const String prefix = String(kCookieName) + "=";
  int searchFrom = 0;
  while (searchFrom < static_cast<int>(cookie.length())) {
    int start = cookie.indexOf(prefix, searchFrom);
    if (start < 0) return String();
    if (start == 0 || cookie[start - 1] == ' ' || cookie[start - 1] == ';') {
      const int valueStart = start + static_cast<int>(prefix.length());
      int valueEnd = cookie.indexOf(';', valueStart);
      if (valueEnd < 0) valueEnd = static_cast<int>(cookie.length());
      String token = cookie.substring(valueStart, valueEnd);
      token.trim();
      return token;
    }
    searchFrom = start + static_cast<int>(prefix.length());
  }
  return String();
}

void clearSession() {
  sessionToken = "";
  sessionAdministrator = false;
  sessionClientBound = false;
  sessionClientIp = IPAddress(0, 0, 0, 0);
}

bool isAuthenticated() {
  if (sessionToken.length() == 0U) return false;

  const String suppliedToken = requestSessionToken();
  const bool cookieMatches = suppliedToken.length() > 0U && suppliedToken == sessionToken;
  const IPAddress remoteIp = server.client().remoteIP();
  const bool clientMatches = sessionClientBound && remoteIp == sessionClientIp;
  if (!cookieMatches && !clientMatches) return false;

  return true;
}

bool requireAuthentication() {
  if (isAuthenticated()) return true;
  sendJson(401, "{\"ok\":false,\"message\":\"登录已失效，请重新打开192.168.4.1\"}");
  return false;
}

bool requireAdministrator() {
  if (!isAuthenticated()) {
    sendJson(401, "{\"ok\":false,\"message\":\"登录已失效，请重新打开192.168.4.1\"}");
    return false;
  }
  if (sessionAdministrator) return true;
  sendJson(404, "{\"ok\":false,\"message\":\"接口不存在\"}");
  return false;
}

void createSession() {
  char token[25] = {};
  snprintf(token,
           sizeof(token),
           "%08lX%08lX%08lX",
           static_cast<unsigned long>(RuntimeProfile::sessionMask(esp_random())),
           static_cast<unsigned long>(RuntimeProfile::sessionMask(esp_random() ^ millis())),
           static_cast<unsigned long>(RuntimeProfile::sessionMask(esp_random() ^ ESP.getFreeHeap())));
  sessionToken = token;
  sessionClientIp = server.client().remoteIP();
  sessionClientBound = true;
}

bool normalizeMac(const String &input, char output[18]) {
  char hex[13] = {};
  size_t count = 0;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (isxdigit(static_cast<unsigned char>(c))) {
      if (count >= 12U) return false;
      hex[count++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    } else if (c != ':' && c != '-' && c != ' ') {
      return false;
    }
  }
  if (count != 12U) return false;
  snprintf(output,
           18,
           "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
           hex[0], hex[1], hex[2], hex[3], hex[4], hex[5],
           hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
  return true;
}

bool parseCoefficient(const String &input, float &value) {
  String normalized = input;
  normalized.trim();
  if (normalized.length() == 0U) return false;

  char *end = nullptr;
  value = strtof(normalized.c_str(), &end);
  if (end == normalized.c_str() || *end != '\0' || !isfinite(value)) {
    return false;
  }

  return value >= AppConfig::Range::MinimumKilometersPerAmpHour &&
         value <= AppConfig::Range::MaximumKilometersPerAmpHour;
}

bool parseMinuteValue(const String &input, uint8_t &value) {
  String normalized = input;
  normalized.trim();
  if (normalized.length() == 0U) return false;
  char *end = nullptr;
  const long parsed = strtol(normalized.c_str(), &end, 10);
  if (end == normalized.c_str() || *end != '\0' || parsed < 0L || parsed > 255L) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

bool validDeviceName(const String &name) {
  if (name.length() > AppConfig::Ble::MaxStoredDeviceNameLength) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    if (static_cast<uint8_t>(name[i]) < 0x20U) return false;
  }
  return true;
}

bool findCurrentScannedDevice(const char *address,
                              char *canonicalName,
                              size_t canonicalNameSize) {
  if (address == nullptr || address[0] == '\0' ||
      canonicalName == nullptr || canonicalNameSize == 0U) {
    return false;
  }

  BmsBleService::ScanDevice devices[AppConfig::Ble::MaxConfigurationResults];
  const size_t count = BmsBleService::copyScanResults(
      devices, AppConfig::Ble::MaxConfigurationResults);
  for (size_t i = 0; i < count; ++i) {
    if (strcasecmp(devices[i].address, address) != 0) continue;
    snprintf(canonicalName, canonicalNameSize, "%s", devices[i].name);
    return true;
  }
  return false;
}

bool parseSubmittedBmsType(BmsType &type) {
  String rawType = server.arg("type");
  rawType.trim();
  return BmsTypeInfo::parse(rawType.c_str(), type);
}

void handleRoot() {
  if (isAuthenticated()) sendConfigPage();
  else sendLoginPage(false);
}

void handleLogin() {
  noteAccess();
  const String submitted = server.arg("password");
  const bool administratorLogin = matchesAdministratorPassword(submitted.c_str());
  if (!administratorLogin && submitted != RuntimeSettings::webPassword()) {
    DiagnosticLog::printf("Web login rejected from %s.\n",
                          server.client().remoteIP().toString().c_str());
    sendLoginPage(true);
    return;
  }

  createSession();
  sessionAdministrator = administratorLogin;
  DiagnosticLog::printf("Web login accepted from %s.\n",
                        server.client().remoteIP().toString().c_str());
  server.sendHeader("Set-Cookie",
                    String(kCookieName) + "=" + sessionToken + "; Path=/; HttpOnly");
  server.sendHeader("Location", "/config");
  addCommonHeaders();
  server.send(303, "text/plain; charset=utf-8", "");
}

void handleConfig() {
  if (!isAuthenticated()) {
    sendLoginPage(false);
    return;
  }
  sendConfigPage();
}

void handleHealth() {
  noteAccess();
  addCommonHeaders();
  String text;
  text.reserve(240);
  text = F("BMS WEB OK\nVersion=v1.0.0\nDeveloper=无用脑洞研究所\nIP=192.168.4.1\nSSID=");
  text += apName;
  text += F("\nStations=");
  text += String(WiFi.softAPgetStationNum());
  text += F("\nHeap=");
  text += String(ESP.getFreeHeap());
  text += F("\nUptimeMs=");
  text += String(millis());
  text += F("\nMode=pre-scan-cached-selector");
  text += F("\nBLEReady=");
  text += (BmsBleService::hasValidStatus() ? "yes" : "no");
  server.send(200, "text/plain; charset=utf-8", text);
}

void handleStatus() {
  if (!requireAuthentication()) return;

  const BmsType pairedType = BmsBleService::pairedBmsType();
  const BmsType scanType = BmsBleService::configurationScanType();

  String payload;
  payload.reserve(360);
  payload = "{\"ok\":true";
  payload += ",\"pairedAddress\":\"" + jsonEscape(BmsBleService::pairedDeviceAddress()) + "\"";
  payload += ",\"pairedType\":\"" + String(BmsTypeInfo::key(pairedType)) + "\"";
  payload += ",\"scanType\":\"" + String(BmsTypeInfo::key(scanType)) + "\"";
  payload += ",\"rangeCoefficient\":" + String(RuntimeSettings::rangeCoefficientKmPerAh(), 3);
  const RuntimeSettings::DisplayPowerSettings displaySettings =
      RuntimeSettings::displayPowerSettings();
  payload += ",\"autoDisplayEnabled\":" +
      String(displaySettings.enabled ? "true" : "false");
  payload += ",\"sleepMinutes\":" + String(displaySettings.sleepMinutes);
  payload += ",\"apName\":\"" + jsonEscape(apName) + "\"";
  payload += "}";
  sendJson(200, payload);
}

void handleDevices() {
  if (!requireAuthentication()) return;

  BmsBleService::ScanDevice devices[AppConfig::Ble::MaxConfigurationResults];
  const size_t count = BmsBleService::copyScanResults(
      devices, AppConfig::Ble::MaxConfigurationResults);
  const BmsType scanType = BmsBleService::configurationScanType();

  String payload;
  payload.reserve(330U + count * 112U);
  payload = "{\"ok\":true,\"scanType\":\"";
  payload += BmsTypeInfo::key(scanType);
  payload += "\",\"pairedAddress\":\"";
  payload += jsonEscape(BmsBleService::pairedDeviceAddress());
  payload += "\",\"devices\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0U) payload += ',';
    payload += "{\"name\":\"";
    payload += jsonEscape(devices[i].name);
    payload += "\",\"address\":\"";
    payload += jsonEscape(devices[i].address);
    payload += "\",\"rssi\":";
    payload += String(devices[i].rssi);
    payload += '}';
  }
  payload += "]}";
  sendJson(200, payload);
}

void handleSave() {
  if (!requireAuthentication()) return;

  BmsType selectedType = BmsType::Ant;
  if (!parseSubmittedBmsType(selectedType)) {
    sendJson(400, "{\"ok\":false,\"message\":\"保护板品牌参数无效\"}");
    return;
  }

  if (selectedType != BmsBleService::configurationScanType()) {
    sendJson(409, "{\"ok\":false,\"message\":\"品牌已切换，请先按所选品牌重新扫描\"}");
    return;
  }

  String rawAddress = server.arg("address");
  String name = server.arg("name");
  rawAddress.trim();
  name.trim();

  if (rawAddress.length() == 0U && name.length() == 0U) {
    sendJson(400, "{\"ok\":false,\"message\":\"蓝牙名称和MAC地址至少填写一项\"}");
    return;
  }
  if (!validDeviceName(name)) {
    sendJson(400, "{\"ok\":false,\"message\":\"蓝牙名称过长或包含无效字符\"}");
    return;
  }

  char normalizedAddress[18] = {};
  if (rawAddress.length() == 0U || !normalizeMac(rawAddress, normalizedAddress)) {
    sendJson(400, "{\"ok\":false,\"message\":\"请从当前扫描列表选择保护板\"}");
    return;
  }

  char scannedName[AppConfig::Ble::MaxStoredDeviceNameLength + 1U] = {};
  if (!findCurrentScannedDevice(normalizedAddress, scannedName, sizeof(scannedName))) {
    sendJson(409, "{\"ok\":false,\"message\":\"该设备不在当前品牌扫描列表中，请重新扫描\"}");
    return;
  }

  name = scannedName;

  float coefficient = 0.0f;
  if (!parseCoefficient(server.arg("coefficient"), coefficient)) {
    sendJson(400, "{\"ok\":false,\"message\":\"里程系数必须在0.01到100之间\"}");
    return;
  }

  const float previousCoefficient = RuntimeSettings::rangeCoefficientKmPerAh();
  if (!RuntimeSettings::setRangeCoefficientKmPerAh(coefficient)) {
    sendJson(500, "{\"ok\":false,\"message\":\"里程系数写入失败\"}");
    return;
  }

  if (!BmsBleService::saveConfiguredTargetForRestart(
          selectedType, normalizedAddress, name.c_str())) {

    RuntimeSettings::setRangeCoefficientKmPerAh(previousCoefficient);
    sendJson(500, "{\"ok\":false,\"message\":\"保护板品牌或目标参数写入失败\"}");
    return;
  }

  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  rebootAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::WebConfig::SaveRestartDelayMs);
}

void handleDisplaySettings() {
  if (!requireAuthentication()) return;

  String enabledText = server.arg("enabled");
  enabledText.trim();
  if (enabledText != "0" && enabledText != "1") {
    sendJson(400, "{\"ok\":false,\"message\":\"启用参数无效\"}");
    return;
  }

  uint8_t sleepMinutes = 0U;
  if (!parseMinuteValue(server.arg("sleepMinutes"), sleepMinutes) ||
      !RuntimeSettings::isValidDisplayPowerSettings(sleepMinutes)) {
    sendJson(400, "{\"ok\":false,\"message\":\"休眠时间需为5-10分钟\"}");
    return;
  }

  if (!RuntimeSettings::setDisplayPowerSettings(
          enabledText == "1", sleepMinutes)) {
    sendJson(500, "{\"ok\":false,\"message\":\"屏幕设置写入失败\"}");
    return;
  }

  sendJson(200, "{\"ok\":true}");
}

void handleRescan() {
  if (!requireAuthentication()) return;

  BmsType selectedType = BmsType::Ant;
  if (!parseSubmittedBmsType(selectedType)) {
    sendJson(400, "{\"ok\":false,\"message\":\"保护板品牌参数无效\"}");
    return;
  }
  if (!RuntimeSettings::requestConfigurationScanOnNextBoot(selectedType)) {
    sendJson(500, "{\"ok\":false,\"message\":\"重新扫描品牌写入失败\"}");
    return;
  }

  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  rebootAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::WebConfig::RescanRestartDelayMs);
}

void handlePassword() {
  if (!requireAuthentication()) return;
  const String password = server.arg("password");
  if (!RuntimeSettings::isValidWebPassword(password.c_str())) {
    sendJson(400, "{\"ok\":false,\"message\":\"密码必须为6-16位数字或英文字母\"}");
    return;
  }
  if (matchesAdministratorPassword(password.c_str())) {
    sendJson(400, "{\"ok\":false,\"message\":\"该密码不可用，请设置其他密码\"}");
    return;
  }
  if (!RuntimeSettings::setWebPassword(password.c_str())) {
    sendJson(500, "{\"ok\":false,\"message\":\"密码写入失败\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handleTestModeStatus() {
  if (!requireAdministrator()) return;
  String payload = "{\"ok\":true,\"enabled\":";
  payload += RuntimeSettings::dashboardTestModeEnabled() ? "true" : "false";
  payload += "}";
  sendJson(200, payload);
}

void handleTestMode() {
  if (!requireAdministrator()) return;

  String enabledText = server.arg("enabled");
  enabledText.trim();
  if (enabledText != "0" && enabledText != "1") {
    sendJson(400, "{\"ok\":false,\"message\":\"测试模式参数无效\"}");
    return;
  }

  const bool enabled = enabledText == "1";
  if (!RuntimeSettings::setDashboardTestModeEnabled(enabled)) {
    sendJson(500, "{\"ok\":false,\"message\":\"测试模式设置写入失败\"}");
    return;
  }

  sendJson(200, "{\"ok\":true,\"rebooting\":true}");
  rebootAt = TimeUtils::deadlineAfter(
      millis(), AppConfig::WebConfig::SaveRestartDelayMs);
}

void handleLogout() {
  clearSession();
  server.sendHeader("Set-Cookie",
                    String(kCookieName) +
                        "=; Path=/; Max-Age=0; HttpOnly");
  sendLoginPage(false);
}

void handleEmptyAsset() {
  noteAccess();
  addCommonHeaders();
  server.send(204, "text/plain", "");
}

void handleCaptiveProbe() {
  if (isAuthenticated()) {
    noteAccess();
    server.sendHeader("Location", "/config");
    addCommonHeaders();
    server.send(302, "text/plain; charset=utf-8", "");
    return;
  }
  sendLoginPage(false);
}

bool startAccessPointRadio() {
  const IPAddress localIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(AppConfig::WebConfig::RadioSettleDelayMs);
  WiFi.persistent(false);

  for (uint8_t attempt = 1;
       attempt <= AppConfig::WebConfig::StartupAttempts;
       ++attempt) {
    if (WiFi.mode(WIFI_AP)) {
      delay(AppConfig::WebConfig::RadioSettleDelayMs);
      const bool configured = WiFi.softAPConfig(localIp, gateway, subnet);
      const bool started = configured &&
          WiFi.softAP(apName,
                      nullptr,
                      AppConfig::WebConfig::ApChannel,
                      false,
                      AppConfig::WebConfig::MaxStations);
      if (started) {
        delay(AppConfig::WebConfig::RadioSettleDelayMs);
        const esp_err_t powerResult = esp_wifi_set_max_tx_power(
            AppConfig::WebConfig::SoftApTxPowerQuarterDbm);
        DiagnosticLog::printf("SoftAP TX power limit result=%d.\n",
                              static_cast<int>(powerResult));
        return true;
      }
      DiagnosticLog::printf("SoftAP start failed (attempt %u, config=%s).\n",
                            static_cast<unsigned>(attempt),
                            configured ? "ok" : "failed");
    } else {
      DiagnosticLog::printf("SoftAP mode start failed (attempt %u).\n",
                            static_cast<unsigned>(attempt));
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(AppConfig::WebConfig::StartupRetryDelayMs);
  }
  return false;
}

void configureRoutes() {
  if (routesConfigured) return;
  const char *headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);

  server.on("/", HTTP_ANY, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/logout", HTTP_ANY, handleLogout);
  server.on("/health", HTTP_ANY, handleHealth);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/devices", HTTP_GET, handleDevices);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/display", HTTP_POST, handleDisplaySettings);
  server.on("/api/rescan", HTTP_POST, handleRescan);
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/test-mode", HTTP_GET, handleTestModeStatus);
  server.on("/api/test-mode", HTTP_POST, handleTestMode);

  server.on("/generate_204", HTTP_ANY, handleCaptiveProbe);
  server.on("/gen_204", HTTP_ANY, handleCaptiveProbe);
  server.on("/hotspot-detect.html", HTTP_ANY, handleCaptiveProbe);
  server.on("/ncsi.txt", HTTP_ANY, handleCaptiveProbe);
  server.on("/connecttest.txt", HTTP_ANY, handleCaptiveProbe);
  server.on("/redirect", HTTP_ANY, handleCaptiveProbe);
  server.on("/fwlink", HTTP_ANY, handleCaptiveProbe);
  server.on("/canonical.html", HTTP_ANY, handleCaptiveProbe);
  server.on("/success.txt", HTTP_ANY, handleCaptiveProbe);
  server.on("/library/test/success.html", HTTP_ANY, handleCaptiveProbe);
  server.on("/favicon.ico", HTTP_ANY, handleEmptyAsset);
  server.on("/apple-touch-icon.png", HTTP_ANY, handleEmptyAsset);
  server.on("/apple-touch-icon-precomposed.png", HTTP_ANY, handleEmptyAsset);
  server.on("/robots.txt", HTTP_ANY, handleEmptyAsset);
  server.onNotFound(handleCaptiveProbe);
  routesConfigured = true;
}

void stopPortalRadioNow() {
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  clearSession();
  keepAliveWhileActive = false;
  accessObserved = false;
  startedAt = 0;
  rebootAt = 0;
  active = false;
}

}

bool begin(bool keepAlive) {
  if (active) {
    keepAliveWhileActive = keepAliveWhileActive || keepAlive;
    return true;
  }

  static_assert(sizeof(AppConfig::WebConfig::AccessPointName) - 1U <= 32U,
                "SoftAP SSID must not exceed 32 bytes");
  snprintf(apName,
           sizeof(apName),
           "%s",
           AppConfig::WebConfig::AccessPointName);

  if (!startAccessPointRadio()) {
    WiFi.mode(WIFI_OFF);
    DiagnosticLog::write("Web configuration SoftAP could not be started after retries.\n");
    return false;
  }

  configureRoutes();
  server.begin();
  const IPAddress localIp(192, 168, 4, 1);
  if (!dnsServer.start(53, "*", localIp)) {
    DiagnosticLog::write("Captive portal DNS server failed to start.\n");
  }

  startedAt = millis();
  accessObserved = false;
  clearSession();
  keepAliveWhileActive = keepAlive;
  rebootAt = 0;
  active = true;

  DiagnosticLog::printf(
      "Cached-scan Web configuration AP ready: SSID=%s, URL=http://192.168.4.1, heap=%u.\n",
      apName,
      static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

void loop() {
  if (!active) return;

  dnsServer.processNextRequest();
  server.handleClient();

  const uint32_t now = millis();
  if (TimeUtils::deadlineReached(now, rebootAt)) {
    rebootAt = 0;
    delay(AppConfig::WebConfig::RebootFlushDelayMs);
    ESP.restart();
    return;
  }

  if (WiFi.softAPgetStationNum() > 0U) accessObserved = true;
  if (!keepAliveWhileActive && BmsBleService::hasConfiguredDevice() && !accessObserved &&
      TimeUtils::elapsedSince(now, startedAt) >=
          AppConfig::WebConfig::InitialAccessWindowMs) {
    DiagnosticLog::write("Recovery Web AP closing: no client connected within window.\n");
    stopPortalRadioNow();
  }
}

bool isActive() {
  return active;
}

bool hasClient() {
  return active && WiFi.softAPgetStationNum() > 0U;
}

void stop() {
  if (!active) return;
  stopPortalRadioNow();
  DiagnosticLog::write("Web configuration AP stopped.\n");
}

}
