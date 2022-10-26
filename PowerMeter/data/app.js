// used when hosting the site on the ESP8266
let address = location.hostname;
let urlBase = "";

// used when hosting the site somewhere other than the ESP8266 (handy for testing without waiting forever to upload to SPIFFS)
// let address = "192.168.86.49";
// let urlBase = "http://" + address + "/";

var ws = new ReconnectingWebSocket("ws://" + address + ":81/", ["arduino"]);
ws.debug = true;

const currentData = [];
const voltageData = [];
let lineChart;

let voltage = 0;
let minVoltage = 1000;
let maxVoltage = -1000;

let current = 0;
let minCurrent = 1000;
let maxCurrent = -1000;

let lastVoltage;
let lastCurrent;

let timestamp = 0;
let updated = false;

ws.onmessage = function (evt) {
  if (evt.data != null) {
    var data = JSON.parse(evt.data);
    if (data == null) return;
    // console.log({ data });

    const { v, c, minV, maxV, minC, maxC } = data;
    if (v) {
      if (v !== lastVoltage) {
        lastVoltage = v;
        updated = true;
      }
      voltage = v;
      $("#voltage").html(`${v}V`);
    }
    if (c) {
      if (c !== lastCurrent) {
        lastCurrent = c;
        updated = true;
      }
      current = c;
      $("#current").html(`${c}A`);
    }

    if (minV) {
      minVoltage = minV;
      $("#minVoltage").html(`${minVoltage}V`);
    }
    if (maxV) {
      maxVoltage = maxV;
      $("#maxVoltage").html(`${maxVoltage}V`);
    }
    if (minC) {
      minCurrent = minC;
      $("#minCurrent").html(`${minCurrent}V`);
    }
    if (maxC) {
      maxCurrent = maxC;
      $("#maxCurrent").html(`${maxCurrent}V`);
    }

    if (v || c) {
      timestamp = new Date().getTime();
    }
  }
};

function updateChart() {
  if (updated) {
    voltageData.push({ value: voltage, timestamp });
    currentData.push({ value: current, timestamp });
    lineChart.update();

    $("#tbody").prepend(`<tr>
  <td>${new Date().toLocaleString()}</td>
  <td>${voltage}</td>
  <td>${current}</td>
</tr>`);

    updated = false;
  }
}

$(document).ready(function () {
  $("#status").html("Connecting, please wait...");

  const data = {
    datasets: [
      {
        label: "Voltage",
        backgroundColor: "rgb(255, 99, 132)",
        borderColor: "rgb(255, 99, 132)",
        data: voltageData,
      },
      {
        label: "Current",
        backgroundColor: "rgb(64, 99, 132)",
        borderColor: "rgb(64, 99, 132)",
        data: currentData,
      },
    ],
  };

  const config = {
    type: "line",
    data: data,
    options: {
      animation: false,
      elements: {
        point: {
          hitRadius: 4,
          hoverRadius: 8,
          radius: 0,
        },
      },
      parsing: {
        xAxisKey: "timestamp",
        yAxisKey: "value",
      },
      responsive: true,
      scales: {
        x: { type: "timeseries" },
        y: {
          beginAtZero: true,
          type: "linear",
        },
      },
    },
  };

  lineChart = new Chart(document.getElementById("lineChart"), config);

  setInterval(updateChart, 1000);

  $("#status").html("Connected");
});
