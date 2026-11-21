// kWeather phone side: location -> Open-Meteo forecast (no key) -> one message to the watch.
var NH = 24, ND = 7;

function get(url, ok, fail) {
  var x = new XMLHttpRequest();
  x.onload = function () {
    if (x.status >= 200 && x.status < 300) { try { ok(JSON.parse(x.responseText)); return; } catch (e) {} }
    fail('Server error ' + x.status);
  };
  x.onerror = function () { fail('No internet'); };
  x.timeout = 20000; x.ontimeout = function () { fail('Timed out'); };
  x.open('GET', url); x.send();
}

function u8(v) { v = Math.round(v || 0); return v < 0 ? 0 : v > 255 ? 255 : v; }
function err(m) { Pebble.sendAppMessage({ ERR: m }); }

function send(city, d) {
  var cur = d.current, h = d.hourly, dl = d.daily;
  var key = cur.time.substr(0, 13), i0 = 0;
  for (var i = 0; i < h.time.length; i++) if (h.time[i].substr(0, 13) === key) { i0 = i; break; }
  var ht = [], hp = [], hmm = [], hc = [];
  for (var j = 0; j < NH; j++) {
    var k = Math.min(i0 + j, h.time.length - 1);
    ht.push(u8(h.temperature_2m[k] + 100));
    hp.push(u8(h.precipitation_probability[k]));
    hmm.push(u8(h.precipitation[k] * 10));
    hc.push(u8(h.weather_code[k]));
  }
  var dc = [], dp = [], dhi = [], dlo = [];
  for (var n = 0; n < ND; n++) {
    dc.push(u8(dl.weather_code[n])); dp.push(u8(dl.precipitation_probability_max[n]));
    dhi.push(u8(dl.temperature_2m_max[n] + 100)); dlo.push(u8(dl.temperature_2m_min[n] + 100));
  }
  Pebble.sendAppMessage({
    CITY: (city || '').substr(0, 22),
    T: Math.round(cur.temperature_2m), FEELS: Math.round(cur.apparent_temperature),
    CODE: cur.weather_code, ISDAY: cur.is_day,
    HI: Math.round(dl.temperature_2m_max[0]), LO: Math.round(dl.temperature_2m_min[0]),
    WIND: Math.round(cur.wind_speed_10m), HUM: Math.round(cur.relative_humidity_2m),
    HOUR0: parseInt(h.time[i0].substr(11, 2), 10),
    WDAY0: new Date(dl.time[0] + 'T12:00:00').getDay(),
    HT: ht, HP: hp, HMM: hmm, HC: hc, DC: dc, DP: dp, DHI: dhi, DLO: dlo
  }, function () {}, function () { setTimeout(function () { send(city, d); }, 2000); });
}

function fetchFor(lat, lon) {
  var wx = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat.toFixed(3) + '&longitude=' + lon.toFixed(3) +
    '&current=temperature_2m,apparent_temperature,weather_code,relative_humidity_2m,wind_speed_10m,is_day' +
    '&hourly=temperature_2m,precipitation_probability,precipitation,weather_code' +
    '&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max' +
    '&timezone=auto&forecast_days=7';
  var place = 'https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=' + lat.toFixed(3) +
    '&longitude=' + lon.toFixed(3) + '&localityLanguage=en';
  get(place, function (p) {
    var city = p.city || p.locality || p.principalSubdivision || '';
    get(wx, function (d) { send(city, d); }, err);
  }, function () { get(wx, function (d) { send('', d); }, err); });
}

function refresh() {
  navigator.geolocation.getCurrentPosition(function (pos) {
    var c = pos.coords;
    localStorage.setItem('loc', JSON.stringify([c.latitude, c.longitude]));
    fetchFor(c.latitude, c.longitude);
  }, function () {
    var last = null;
    try { last = JSON.parse(localStorage.getItem('loc')); } catch (e) {}
    if (last) fetchFor(last[0], last[1]); else err('Turn on location');
  }, { timeout: 15000, maximumAge: 600000 });
}

Pebble.addEventListener('ready', refresh);
Pebble.addEventListener('appmessage', function (e) { if (e.payload.CMD !== undefined) refresh(); });
