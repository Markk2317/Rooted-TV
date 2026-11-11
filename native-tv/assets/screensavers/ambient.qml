/*
 * Google TV screen saver.
 *
 * Google TV's ambient mode: one full-screen picture after another, each slowly
 * panning and crossfading into the next, its caption in the lower left (a small
 * capitalised overline over the name) and the time and the weather in the lower
 * right, in Google Sans. The pictures are the owner's photos where some are
 * uploaded on the dashboard's Screensaver tab, else the stills of the titles on
 * Native Tv Home's banner and trending rows. The text moves a few pixels every
 * minute, so on an OLED no pixel holds the same text for long.
 * The pictures and the weather come from the server, at the address written in
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

    // Native Tv's own copy of the client's face; LG's Museo Sans where it cannot be read.
    FontLoader { id: sansFace; source: "file:///var/lib/tvweb/assets/fonts/GoogleSansFlex-Regular.ttf" }
    FontLoader { id: lightFace; source: "file:///usr/share/fonts/MuseoSans-Light.ttf" }
    property string face: sansFace.status === FontLoader.Ready ? sansFace.name
                        : lightFace.status === FontLoader.Ready ? lightFace.name : ""

    property real unit: win.height / 1080
    // Dim or bright, written in when the screen saver is staged: dim keeps the pictures a
    // little darker, for a saver that is on for hours.
    property int level: __TVWEB_LEVEL__
    property bool hour12: false
    property string origin: "__TVWEB_ORIGIN__"
    property string key: "__TVWEB_KEY__"

    // [{path, overline, title}]
    property var pictures: []
    property int shown: -1
    property bool frontA: true
    property string temperature: ""
    property string weatherWords: ""
    property string overline: ""
    property string caption: ""
    // Where the text sits, within 16 px of its place: one of nine spots, a new one every minute.
    property var spots: [0, 8, -8, 16, -16, 4, -4, 12, -12]
    property int spot: 0
    property int drift: spots[spot]
    // Pictures that would not load in a row: past the list's length, the next waits for the timer.
    property int failures: 0

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
    function loadPictures() {
        get("/api/screensaver/photos", function(answer) {
            var list = [];
            var ids = answer && answer.photos && answer.photos.ids ? answer.photos.ids : [];
            for (var i = 0; i < ids.length; ++i)
                list.push({ path: "/api/screensaver/photo?id=" + ids[i], overline: "Photos",
                            title: answer.city || "" });
            if (!list.length && answer && answer.ambient) list = answer.ambient;
            var first = !win.pictures.length;
            win.pictures = list;
            if (first && list.length) nextPicture();
        });
    }
    function loadWeather() {
        get("/api/screensaver/weather", function(answer) {
            if (!answer || !answer.ok) { win.temperature = ""; win.weatherWords = ""; return; }
            win.temperature = answer.temperature + "°";
            win.weatherWords = answer.words || "";
        });
    }
    // Any picture but the one on screen, so a short list still changes every time.
    function nextPicture() {
        if (!pictures.length) return;
        var pick = Math.floor(Math.random() * pictures.length);
        if (pictures.length > 1 && pick === shown) pick = (pick + 1) % pictures.length;
        shown = pick;
        var back = frontA ? pictureB : pictureA;
        back.pending = pictures[pick];
        back.source = address(pictures[pick].path);
    }
    function two(n) { return n < 10 ? "0" + n : "" + n }
    function timeText() {
        var now = new Date(), hours = now.getHours();
        if (hour12) hours = hours % 12 === 0 ? 12 : hours % 12;
        return hours + ":" + two(now.getMinutes());
    }

    // Two layers: the one behind takes the next picture and fades in over the other once loaded.
    Image {
        id: pictureA
        property var pending: null
        x: 0; y: 0
        width: win.width; height: win.height
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        sourceSize.width: 1920; sourceSize.height: 1080
        opacity: 0
        onStatusChanged: {
            if (status === Image.Ready && !win.frontA) win.reveal(pictureA, pictureB)
            else if (status === Image.Error) win.failed()
        }
    }
    Image {
        id: pictureB
        property var pending: null
        x: 0; y: 0
        width: win.width; height: win.height
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        sourceSize.width: 1920; sourceSize.height: 1080
        opacity: 0
        onStatusChanged: {
            if (status === Image.Ready && win.frontA) win.reveal(pictureB, pictureA)
            else if (status === Image.Error) win.failed()
        }
    }
    function failed() {
        if (++failures < pictures.length) nextPicture();
    }
    function reveal(incoming, outgoing) {
        failures = 0;
        incoming.z = 1; outgoing.z = 0;
        fadeIn.target = incoming; fadeOut.target = outgoing;
        push.target = incoming; pan.target = incoming;
        // Ken Burns: each picture drifts the other way from the one before.
        pan.from = frontA ? -24 * unit : 24 * unit;
        pan.to = -pan.from;
        fadeIn.restart(); fadeOut.restart(); push.restart(); pan.restart();
        frontA = incoming === pictureA;
        captionOut.restart();
        nextCaption = incoming.pending;
    }
    property var nextCaption: null
    NumberAnimation { id: fadeIn; property: "opacity"; from: 0; to: 1; duration: 1800; easing.type: Easing.InOutQuad }
    NumberAnimation { id: fadeOut; property: "opacity"; to: 0; duration: 1800; easing.type: Easing.InOutQuad }
    // The slow pan of Google TV's ambient pictures: a little larger than the screen, moving sideways.
    NumberAnimation { id: push; property: "scale"; from: 1.06; to: 1.1; duration: 32000; easing.type: Easing.Linear }
    NumberAnimation { id: pan; property: "x"; duration: 32000; easing.type: Easing.Linear }

    // The caption leaves with the old picture and comes back with the new one's.
    SequentialAnimation {
        id: captionOut
        NumberAnimation { target: captionBlock; property: "opacity"; to: 0; duration: 400 }
        ScriptAction {
            script: {
                win.overline = win.nextCaption ? (win.nextCaption.overline || "") : "";
                win.caption = win.nextCaption ? (win.nextCaption.title || "") : "";
            }
        }
        PauseAnimation { duration: 600 }
        NumberAnimation { target: captionBlock; property: "opacity"; to: 1; duration: 900 }
    }

    // Darker at the foot, where the caption and the time sit, and a little darker all over when dim.
    Rectangle {
        anchors.fill: parent
        z: 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 0.6; color: "#00000000" }
            GradientStop { position: 1.0; color: "#99000000" }
        }
    }
    Rectangle { anchors.fill: parent; z: 2; color: "black"; opacity: win.level > 0 ? 0.0 : 0.25 }

    // Google's margins: 116 px from the sides, the text's foot 96 px above the bottom.
    Column {
        id: captionBlock
        z: 3
        x: Math.round((116 + win.drift) * win.unit)
        y: win.height - height - Math.round((96 + win.drift / 2) * win.unit)
        width: Math.round(1000 * win.unit)
        spacing: Math.round(8 * win.unit)
        Behavior on x { NumberAnimation { duration: 4000; easing.type: Easing.InOutQuad } }
        Text {
            visible: win.overline.length > 0
            color: "#cfd6dc"
            font.family: win.face
            font.pixelSize: Math.round(20 * win.unit)
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Math.round(2 * win.unit)
            text: win.overline
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            color: "#f1f4f6"
            font.family: win.face
            font.pixelSize: Math.round(48 * win.unit)
            text: win.caption
        }
    }

    Column {
        id: glance
        z: 3
        x: win.width - width - Math.round((116 + win.drift) * win.unit)
        y: win.height - height - Math.round((96 + win.drift / 2) * win.unit)
        spacing: Math.round(6 * win.unit)
        Behavior on x { NumberAnimation { duration: 4000; easing.type: Easing.InOutQuad } }
        Text {
            id: clock
            x: glance.width - width
            color: "#f1f4f6"
            font.family: win.face
            font.pixelSize: Math.round(72 * win.unit)
            text: win.timeText()
        }
        Text {
            x: glance.width - width
            visible: win.temperature.length > 0
            color: "#cfd6dc"
            font.family: win.face
            font.pixelSize: Math.round(28 * win.unit)
            text: win.weatherWords.length ? win.temperature + "  ·  " + win.weatherWords : win.temperature
        }
    }

    // Nothing to show yet (no photos and Home has no titles): the time alone, as Google TV's
    // clock-only ambient mode.
    Text {
        z: 3
        visible: win.pictures.length === 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round((420 + win.drift) * win.unit)
        color: "#8a8f96"
        font.family: win.face
        font.pixelSize: Math.round(32 * win.unit)
        text: "Add photos on the Screensaver tab, or a TMDB key for Home's titles."
    }

    Timer { interval: 30000; running: true; repeat: true; onTriggered: { win.failures = 0; win.nextPicture(); } }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: clock.text = win.timeText()
    }
    // Every minute the text moves to another of nine spots within 16 px, for the panel.
    Timer { interval: 60000; running: true; repeat: true; onTriggered: win.spot = (win.spot + 1) % win.spots.length }
    Timer { interval: 30 * 60000; running: true; repeat: true; onTriggered: { win.loadWeather(); win.loadPictures(); } }

    Component.onCompleted: { loadPictures(); loadWeather(); }
}
