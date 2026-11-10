pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Open-Meteo (no API key). The place is Settings.data.weatherLocation, looked
// up with Open-Meteo's geocoder, or the IP's approximate location when empty.
// Refreshes every 20 minutes and on demand.
Singleton {
    id: root

    property bool ready: false
    property bool loading: false
    property string error: ""
    property string place: ""
    property real lat: 0
    property real lon: 0
    property var current: null            // { temp, feels, code, wind, humidity, isDay }
    property var hourly: []               // next 12 h: { hour, temp, code, rain }
    property var daily: []                // 7 days: { day, min, max, code, rain }
    readonly property string unit: Settings.data.weatherUnits === "imperial" ? "°F" : "°C"

    function icon(code, isDay) {
        if (code === 0 || code === 1) return isDay === false ? "moon" : "sun"
        if (code === 2) return "cloud-sun"
        if (code === 3) return "cloud"
        if (code === 45 || code === 48) return "cloud-fog"
        if (code >= 51 && code <= 57) return "cloud-drizzle"
        if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "cloud-rain"
        if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "cloud-snow"
        if (code >= 95) return "cloud-lightning"
        return "cloud"
    }

    function describe(code) {
        const t = { 0: "Clear", 1: "Mostly clear", 2: "Partly cloudy", 3: "Overcast", 45: "Fog", 48: "Rime fog",
            51: "Light drizzle", 53: "Drizzle", 55: "Heavy drizzle", 61: "Light rain", 63: "Rain", 65: "Heavy rain",
            66: "Freezing rain", 67: "Freezing rain", 71: "Light snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains",
            80: "Showers", 81: "Showers", 82: "Violent showers", 85: "Snow showers", 86: "Snow showers",
            95: "Thunderstorm", 96: "Thunderstorm, hail", 99: "Thunderstorm, hail" }
        return t[code] || "—"
    }

    function refresh() {
        if (loading) return
        loading = true
        error = ""
        const where = Settings.data.weatherLocation.trim()
        locate.command = where
            ? ["curl", "-sf", "--max-time", "10", `https://geocoding-api.open-meteo.com/v1/search?count=1&name=${encodeURIComponent(where)}`]
            : ["curl", "-sf", "--max-time", "10", "http://ip-api.com/json/?fields=status,city,lat,lon"]
        locate.running = true
    }

    Process {
        id: locate
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const j = JSON.parse(text)
                    const r = j.results ? j.results[0] : j
                    if (!r || r.lat === undefined && r.latitude === undefined) throw "no place"
                    root.lat = r.latitude !== undefined ? r.latitude : r.lat
                    root.lon = r.longitude !== undefined ? r.longitude : r.lon
                    root.place = r.name || r.city || ""
                    const imperial = Settings.data.weatherUnits === "imperial"
                    forecast.command = ["curl", "-sf", "--max-time", "10",
                        `https://api.open-meteo.com/v1/forecast?latitude=${root.lat}&longitude=${root.lon}`
                        + "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,relative_humidity_2m,is_day"
                        + "&hourly=temperature_2m,weather_code,precipitation_probability"
                        + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                        + `&timezone=auto&forecast_days=7${imperial ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : ""}`]
                    forecast.running = true
                } catch (e) {
                    root.loading = false
                    root.error = "Location not found"
                }
            }
        }
        onExited: code => { if (code !== 0) { root.loading = false; root.error = "Offline" } }
    }

    Process {
        id: forecast
        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false
                try {
                    const j = JSON.parse(text)
                    const c = j.current
                    root.current = { temp: Math.round(c.temperature_2m), feels: Math.round(c.apparent_temperature),
                        code: c.weather_code, wind: Math.round(c.wind_speed_10m), humidity: c.relative_humidity_2m, isDay: c.is_day === 1 }
                    const now = new Date(c.time).getTime()
                    const hours = []
                    for (let i = 0; i < j.hourly.time.length && hours.length < 12; i++) {
                        const t = new Date(j.hourly.time[i]).getTime()
                        if (t < now - 1800000) continue
                        hours.push({ hour: j.hourly.time[i].slice(11, 16), temp: Math.round(j.hourly.temperature_2m[i]),
                            code: j.hourly.weather_code[i], rain: j.hourly.precipitation_probability[i] })
                    }
                    root.hourly = hours
                    root.daily = j.daily.time.map((d, i) => ({ day: d, min: Math.round(j.daily.temperature_2m_min[i]),
                        max: Math.round(j.daily.temperature_2m_max[i]), code: j.daily.weather_code[i],
                        rain: j.daily.precipitation_probability_max[i] }))
                    root.ready = true
                } catch (e) {
                    root.error = "Bad forecast reply"
                }
            }
        }
        onExited: code => { if (code !== 0) { root.loading = false; root.error = "Offline" } }
    }

    Timer { interval: 20 * 60000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.refresh() }
    Connections {
        target: Settings.data
        function onWeatherLocationChanged() { root.refresh() }
        function onWeatherUnitsChanged() { root.refresh() }
    }
}
