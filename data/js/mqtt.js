var address = location.hostname;
var urlBase = "";
var mqttEnabled = false;
var mqttServer = '';
var mqttPrefix = '';

// info, warning, success, danger as serverity, msg = innerHtml of div
function showAlert(serverity, msg) {
  var alert = $('#alert');
  alert.attr('class', 'alert alert-' + serverity);
  alert.html(msg);
  alert.fadeIn();
  setTimeout(() => {
    $('#alert').fadeOut();
  }, 2000);

}

function postValue(name, value) {
  $("#status").html("Setting " + name + ": " + value + ", please wait...");

  var body = { name: name, value: value };

  $.post(urlBase + name + "?value=" + value, body, function (data) {
    if (data.name != null) {
      $("#status").html("Set " + name + ": " + data.name);
    } else {
      $("#status").html("Set " + name + ": " + data);
    }
  });
}


$(document).ready(function () {

  $("#status").html("loading, please wait...");
  // 1. load current config from server
  $.get(urlBase + "all", function (data) {
    $.each(data, function (index, field) {
      if (field.name == 'mqttEnabled') {
        mqttEnabled = field.value != 0;
        $('#mqttEnabled').prop('checked', field.value != 0);
      }
      if (field.name == 'mqttServer') {
        mqttServer = field.value;
        $('#mqttServer').val(field.value);
      }
      if (field.name == 'mqttPrefix') {
        mqttPrefix = field.value;
        $('#mqttPrefix').val(field.value);
      }
      $("#status").html("Ready");
    });
  });
  // 2. let save button post values to server, if changed
  $('#mqttSave').click(() => {
    // validate server name
    var server = $('#mqttServer').val();
    if (mqttServer != server) {
      if (server.includes(':')) {
        showAlert('warning', 'Server must not contain port number (colons)');
      } else {
        postValue('mqttServer', server);
        mqttServer = server;
      }
    }
    // validate prefix
    var prefix = $('#mqttPrefix').val();
    if (prefix != mqttPrefix) {
      if (!prefix.endsWith('/')) {
        showAlert('warning', 'Topic prefix should end with /');
      } else {
        postValue('mqttPrefix', prefix);
        mqttPrefix = prefix;
      }
    }
    var enabled = $('#mqttEnabled').prop('checked');
    if (enabled != mqttEnabled) {
      postValue('mqttEnabled', enabled ? 1 : 0);
      mqttEnabled = enabled;
    }
  });
});
