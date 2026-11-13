/*
 * Photos screen saver.
 *
 * Google TV's ambient mode with the owner's own pictures: the photos uploaded on
 * the dashboard's Screensaver tab, one after another every 20 seconds with a
 * crossfade and a slow push-in, and the time, the date and the weather in a
 * corner that changes every minute, so no pixel holds the same text for long.
 * The photos and the weather come from the server, at the address written in
 * when the screen saver is staged. QtQuick 2.4 and Eos.Window, as the others.
 */
import QtQuick 2.4
import Eos.Window 0.1

WebOSWindow {
    id: win

    width: 1920
    height: 1080

    windowType: "_WEBOS_WINDOW_TYPE_SCREENSAVER"
    appId: "com.webos.app.screensaver"
    title: "Screen Saver"
    visible: true
    color: "black"

    FontLoader { id: lightFace; source: "file:///usr/share/fonts/MuseoSans-Light.ttf" }
    FontLoader { id: thinFace;  source: "file:///usr/share/fonts/MuseoSans-Thin.ttf" }

    property real unit: win.height / 1080
    // Dim or bright, written in when the screen saver is staged: dim keeps the photos a
    // little darker, for a saver that is on for hours.
    property int level: __TVWEB_LEVEL__
    property bool hour12: false
    property string origin: "__TVWEB_ORIGIN__"
    property string key: "__TVWEB_KEY__"

    property var ids: []
    property int shown: -1
    property bool frontA: true
    property string weatherLine: ""
    property int corner: 0

    function address(path) {
        return origin + path + (path.indexOf("?") < 0 ? "?" : "&") + "k=" + key
    }
    function get(path, done) {
        var request = new XMLHttpRequest();
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE) return;
            if (request.status !== 200) return done(null);
            try { done(JSON.parse(request.responseText)); } catch (e) { done(null); }
        };
        request.open("GET", address(path));
        request.send();
    }
    function loadPhotos() {
        get("/api/screensaver/photos", function(answer) {
            win.ids = answer && answer.photos && answer.photos.ids ? answer.photos.ids : [];
            if (win.ids.length) nextPhoto();
        });
    }
    function loadWeather() {
        get("/api/screensaver/weather", function(answer) {
            if (!answer || !answer.ok) { win.weatherLine = ""; return; }
            var parts = [answer.temperature + "°"];
            if (answer.words) parts.push(answer.words);
            if (answer.city) parts.push(answer.city);
            win.weatherLine = parts.join("  ·  ");
        });
    }
    // Any photo but the one on screen, so a short list still changes every time.
    function nextPhoto() {
        if (!ids.length) return;
        var pick = Math.floor(Math.random() * ids.length);
        if (ids.length > 1 && pick === shown) pick = (pick + 1) % ids.length;
        shown = pick;
        var back = frontA ? photoB : photoA;
        back.source = address("/api/screensaver/photo?id=" + ids[pick]);
    }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    function timeText() {
        var now = new Date(), hours = now.getHours();
        if (hour12) hours = hours % 12 === 0 ? 12 : hours % 12;
        return (hour12 ? hours : two(hours)) + ":" + two(now.getMinutes());
    }

    // Two layers: the one behind takes the next photo and fades in over the other once loaded.
    Image {
        id: photoA
        anchors.centerIn: parent
        width: win.width; height: win.height
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        sourceSize.width: 1920; sourceSize.height: 1080
        opacity: 0
        onStatusChanged: if (status === Image.Ready && !win.frontA) win.reveal(photoA, photoB)
    }
    Image {
        id: photoB
        anchors.centerIn: parent
        width: win.width; height: win.height
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        sourceSize.width: 1920; sourceSize.height: 1080
        opacity: 0
        onStatusChanged: if (status === Image.Ready && win.frontA) win.reveal(photoB, photoA)
    }
    function reveal(incoming, outgoing) {
        incoming.z = 1; outgoing.z = 0;
        fadeIn.target = incoming; fadeOut.target = outgoing;
        push.target = incoming;
        fadeIn.restart(); fadeOut.restart(); push.restart();
        frontA = incoming === photoA;
    }
    NumberAnimation { id: fadeIn; property: "opacity"; from: 0; to: 1; duration: 2400; easing.type: Easing.InOutQuad }
    NumberAnimation { id: fadeOut; property: "opacity"; to: 0; duration: 2400; easing.type: Easing.InOutQuad }
    // The slow push-in of Google TV's ambient photos.
    NumberAnimation { id: push; property: "scale"; from: 1.0; to: 1.08; duration: 22000; easing.type: Easing.Linear }

    // Darker at the foot, where the time sits, and a little darker all over when dim.
    Rectangle {
        anchors.fill: parent
        z: 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 0.55; color: "#00000000" }
            GradientStop { position: 1.0; color: "#b0000000" }
        }
    }
    Rectangle { anchors.fill: parent; z: 2; color: "black"; opacity: win.level > 0 ? 0.0 : 0.25 }

    Column {
        id: glance
        z: 3
        spacing: Math.round(4 * win.unit)
        x: win.corner === 0 ? Math.round(96 * win.unit) : win.width - width - Math.round(96 * win.unit)
        y: win.height - height - Math.round(80 * win.unit)
        Behavior on x { NumberAnimation { duration: 2600; easing.type: Easing.InOutQuad } }
        Text {
            id: clock
            color: win.level > 0 ? "#ffffff" : "#d8dce2"
            font.family: thinFace.status === FontLoader.Ready ? thinFace.name : ""
            font.pixelSize: Math.round(132 * win.unit)
            text: win.timeText()
        }
        Text {
            color: win.level > 0 ? "#e6e8ec" : "#a8aeb6"
            font.family: lightFace.status === FontLoader.Ready ? lightFace.name : ""
            font.pixelSize: Math.round(34 * win.unit)
            text: Qt.formatDate(new Date(), "dddd d MMMM")
        }
        Text {
            visible: win.weatherLine.length > 0
            color: win.level > 0 ? "#e6e8ec" : "#a8aeb6"
            font.family: lightFace.status === FontLoader.Ready ? lightFace.name : ""
            font.pixelSize: Math.round(34 * win.unit)
            text: win.weatherLine
        }
    }

    Text {
        z: 3
        visible: win.ids.length === 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(420 * win.unit)
        width: Math.round(1100 * win.unit)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#8a8f96"
        font.family: lightFace.status === FontLoader.Ready ? lightFace.name : ""
        font.pixelSize: Math.round(36 * win.unit)
        text: "Add photos on the Screensaver tab of the Rooted TV dashboard."
    }

    Timer { interval: 20000; running: true; repeat: true; onTriggered: win.nextPhoto() }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: clock.text = win.timeText()
    }
    // The corner changes every minute, as the other savers move, for the panel.
    Timer { interval: 60000; running: true; repeat: true; onTriggered: win.corner = 1 - win.corner }
    Timer { interval: 30 * 60000; running: true; repeat: true; onTriggered: win.loadWeather() }

    Component.onCompleted: { loadPhotos(); loadWeather(); }
}
