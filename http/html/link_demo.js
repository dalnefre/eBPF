$(function () {
    let $inbound = $('#inbound');
    let $pkt_count = $('#pkt_count');
    let $outbound = $('#outbound');
    let $raw_data = $('#raw_data');

    let get_ait = function (s) {
        let i = s.indexOf('\u0000');
        if (i < 0) {
            i = s.length;
        }
        return s.slice(0, i);
    }

    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) {
            $raw_data.addClass('error');
            return;  // prevent overlapping requests
        }
        waiting = true;
        $raw_data.removeClass('error');
        $.getJSON('/ebpf_map/ait.json')
            .done(update);
    };
    let update = function (data) {
        if (data.ait_map[1].n !== -1) {
            // inbound AIT
            var s = $inbound.text();
            s += get_ait(data.ait_map[1].s);
            $inbound.text(s);
            //$inbound.append(document.createTextNode(s));
        }
        $pkt_count.val(data.ait_map[3].n);
        $raw_data.text(JSON.stringify(data, null, 2));
        waiting = false;
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
    // FIXME: rAF = requestAnimationFrame(draw);

    $('#pause').click(function (e) {
        toggleRefresh();
    });

    $('#send').click(function (e) {
        $raw_data.toggleClass('hidden');
    });

    startRefresh();
});
