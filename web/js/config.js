var info = null;

function formatBytes(bytes) {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
  return Math.round(bytes / 1024) + " KB";
}

function formatUptime(seconds) {
  var d = Math.floor(seconds / 86400);
  var h = Math.floor((seconds % 86400) / 3600);
  var m = Math.floor((seconds % 3600) / 60);
  return (d ? d + "d " : "") + h + "h " + m + "m";
}

// severity: info, success, warning, danger
function showAlert(id, severity, msg, autoHide) {
  var alert = $(id);
  alert.attr("class", "alert alert-" + severity);
  alert.text(msg);
  alert.stop(true, true).show();
  if (autoHide) {
    setTimeout(function () { alert.fadeOut(); }, 3000);
  }
}

function renderInfo(d) {
  var rows = [
    ["Firmware", d.version + " (" + d.build + ")"],
    ["LED data pin", d.dataPin + " (GPIO" + d.dataGpio + ")"],
    ["LEDs", d.numLeds + " × " + d.ledType + ", " + d.colorOrder],
    ["Max. current", d.maxMilliAmps + " mA"],
    ["Hostname", d.hostname],
    ["IP / MAC", d.ip + " / " + d.mac],
    ["WiFi", d.ssid + " (" + d.rssi + " dBm)"],
    ["Uptime", formatUptime(d.uptime)],
    ["Free heap", formatBytes(d.freeHeap)],
    ["Flash", formatBytes(d.sketchSize) + " used, " + formatBytes(d.freeSketchSpace) + " free for updates (chip " +
      formatBytes(d.flashSize) + ")"],
    ["ESP8266 core", d.core]
  ];
  var tbody = $("#info tbody").empty();
  rows.forEach(function (row) {
    tbody.append($("<tr>").append($("<th>").text(row[0]), $("<td>").text(row[1])));
  });
}

function renderMqttStatus(d) {
  var status = $("#mqttStatus");
  if (!d.mqttEnabled) {
    status.attr("class", "label label-default pull-right").text("disabled");
  } else if (d.mqttConnected) {
    status.attr("class", "label label-success pull-right").text("connected");
  } else {
    status.attr("class", "label label-warning pull-right").text("not connected");
  }
}

function loadInfo(fillForm) {
  return $.getJSON("info")
    .done(function (d) {
      info = d;
      renderInfo(d);
      renderMqttStatus(d);
      if (fillForm) {
        $("#mqttEnabled").prop("checked", d.mqttEnabled != 0);
        $("#mqttDiscovery").prop("checked", d.mqttDiscovery != 0);
        $("#mqttServer").val(d.mqttServer).attr("maxlength", d.mqttServerMaxLen);
        $("#mqttPrefix").val(d.mqttPrefix).attr("maxlength", d.mqttPrefixMaxLen);
        $("#mqttServerMaxLen").text(d.mqttServerMaxLen);
      }
      $("#status").text("Ready");
    })
    .fail(function () {
      $("#status").text("Cannot reach the lamp");
    });
}

function postValue(name, value) {
  return $.post(name, { value: value });
}

function saveMqtt() {
  var server = $.trim($("#mqttServer").val());
  var prefix = $.trim($("#mqttPrefix").val());
  var enabled = $("#mqttEnabled").prop("checked");
  var discovery = $("#mqttDiscovery").prop("checked");

  if (server.indexOf(":") >= 0) {
    showAlert("#mqttAlert", "warning", "Server must not contain a port number.");
    return;
  }
  if (enabled && server.length == 0) {
    showAlert("#mqttAlert", "warning", "Please enter a server.");
    return;
  }
  if (prefix.length == 0 || prefix.charAt(prefix.length - 1) != "/") {
    showAlert("#mqttAlert", "warning", "Topic prefix must end with /");
    return;
  }

  // sequential: each request writes to the EEPROM and reconnects
  var requests = [];
  if (server != info.mqttServer) requests.push(["mqttServer", server]);
  if (prefix != info.mqttPrefix) requests.push(["mqttPrefix", prefix]);
  if (enabled != (info.mqttEnabled != 0)) requests.push(["mqttEnabled", enabled ? 1 : 0]);
  if (discovery != (info.mqttDiscovery != 0)) requests.push(["mqttDiscovery", discovery ? 1 : 0]);

  if (requests.length == 0) {
    showAlert("#mqttAlert", "info", "Nothing changed.", true);
    return;
  }

  $("#status").text("Saving...");
  var chain = $.Deferred().resolve().promise();
  requests.forEach(function (r) {
    chain = chain.then(function () { return postValue(r[0], r[1]); });
  });
  chain
    .done(function () {
      showAlert("#mqttAlert", "success", "Saved.", true);
      // give the lamp a moment to (re)connect before showing the status
      setTimeout(function () { loadInfo(true); }, 2000);
      loadInfo(false);
    })
    .fail(function () {
      showAlert("#mqttAlert", "danger", "Saving failed.");
      $("#status").text("Saving failed");
    });
}

function setProgress(percent) {
  $("#fwProgress .progress-bar").css("width", percent + "%").text(percent + "%");
}

// after an update the lamp reboots - poll until it answers again, then reload the page
function waitForReboot(startedAt) {
  setTimeout(function () {
    $.ajax({ url: "info", dataType: "json", timeout: 2000 })
      .done(function (d) {
        // rebooted = uptime is shorter than the time since the upload finished
        if (d.uptime * 1000 < Date.now() - startedAt + 5000) {
          showAlert("#fwAlert", "success", "Update done, now running version " + d.version + ". Reloading...");
          setTimeout(function () { location.reload(); }, 1500);
        } else {
          waitForReboot(startedAt);
        }
      })
      .fail(function () {
        if (Date.now() - startedAt > 90000) {
          showAlert("#fwAlert", "warning", "The lamp did not come back yet. Please reload the page in a moment.");
        } else {
          waitForReboot(startedAt);
        }
      });
  }, 3000);
}

function uploadFirmware() {
  var file = $("#fwFile")[0].files[0];
  if (!file) {
    showAlert("#fwAlert", "warning", "Please choose a firmware file (.bin).");
    return;
  }
  if (!/\.bin(\.gz)?$/i.test(file.name)) {
    showAlert("#fwAlert", "warning", "That does not look like a firmware file (.bin).");
    return;
  }
  if (info && file.size > info.freeSketchSpace) {
    showAlert("#fwAlert", "danger", "File too large: " + formatBytes(file.size) + ", only " +
      formatBytes(info.freeSketchSpace) + " available.");
    return;
  }

  var data = new FormData();
  data.append("firmware", file, file.name);

  var xhr = new XMLHttpRequest();
  xhr.open("POST", "update");
  xhr.upload.onprogress = function (e) {
    if (e.lengthComputable) setProgress(Math.round((e.loaded / e.total) * 100));
  };
  xhr.onload = function () {
    if (xhr.status == 200 && /success/i.test(xhr.responseText)) {
      setProgress(100);
      showAlert("#fwAlert", "info", "Upload complete, the lamp is rebooting...");
      $("#status").text("Rebooting...");
      waitForReboot(Date.now());
    } else {
      $("#fwUpload").prop("disabled", false);
      showAlert("#fwAlert", "danger", "Update failed: " + $("<div>").html(xhr.responseText).text());
    }
  };
  xhr.onerror = function () {
    $("#fwUpload").prop("disabled", false);
    showAlert("#fwAlert", "danger", "Upload failed (connection error).");
  };

  $("#fwUpload").prop("disabled", true);
  $("#fwAlert").hide();
  $("#fwProgress").show();
  setProgress(0);
  $("#status").text("Uploading firmware...");
  xhr.send(data);
}

$(document).ready(function () {
  loadInfo(true);
  setInterval(function () { loadInfo(false); }, 10000);

  $("#mqttForm").on("submit", function (e) {
    e.preventDefault();
    saveMqtt();
  });
  $("#fwUpload").click(uploadFirmware);
});
