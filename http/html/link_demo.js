$(function () {
    let $inbond = $('#inbond');
    let $outbound = $('#outbound');
    let $raw_data = $('#raw_data');

    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) return;  // prevent overlapping requests
        waiting = true;
        $.getJSON('/ebpf_map/ait.json')
            .done(function (data) {
                $raw_data.text(JSON.stringify(data, null, 2));
                waiting = false;
            });
    };

    var interval = false;
    let startRefresh = function (delay) {
        delay = delay || 1000;  // default update rate = 1 per second
        refresh();
        interval = setInterval(refresh, delay);
    };
    let stopRefresh = function () {
        clearInterval(interval);
        interval = false;
    };
    let toggleRefresh = function () {
        if (interval) {
            stopRefresh();
        } else {
            startRefresh();
        }
    };

    $('#send').click(function (e) {
        toggleRefresh();
    });

    startRefresh();
});
