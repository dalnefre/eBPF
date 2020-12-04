$(function () {
    let $inbond = $('#inbond');
    let $pkt_count = $('#pkt_count');
    let $outbound = $('#outbound');
    let $raw_data = $('#raw_data');

    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) {
            $raw_data.addClass("error");
            return;  // prevent overlapping requests
        }
        waiting = true;
        $raw_data.removeClass("error");
        $.getJSON('/ebpf_map/ait.json')
            .done(function (data) {
                $pkt_count.val(data.ait_map[3].n);
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
