$(function () {
    let $host = $('#host');
    let $link_stat = $('#link_stat');
    let $pkt_count = $('#pkt_count');
    let $inbound = $('#inbound');
    let $outbound = $('#outbound');
    let $raw_data = $('#raw_data');

    var ait = null;  // outbound AIT
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
        var params = {};
        if (typeof ait === 'string') {
            params.ait = ait;  // FIXME: only send 64 bits at a time?
        }
        $.getJSON('/ebpf_map/ait.json', params)
            .done(update);
    };
    let update = function (data) {
        if (data.ait_map[1].n !== -1) {
            // inbound AIT
            var s = get_ait(data.ait_map[1].s);
            //$inbound.text($inbound.text() + s);
            $inbound.append(document.createTextNode(s));
        }
        if (typeof data.sent === 'string') {
            if (ait.startsWith(data.sent)) {
                ait = ait.slice(data.sent.length);
            } else {
                console.log('WARNING! '
                    + 'expected "' + data.sent + '"'
                    + 'as prefix of "' + ait + '"');
            }
            if (ait.length == 0) {
                ait = null;
            }
        }
/*
.link-dflt { color: #666; background-color: #CCC; }
.link-init { color: #333; background-color: #FF0; }
.link-down { color: #000; background-color: #F00; }
.link-up   { color: #FFF; background-color: #0C0; }
.link-dead { color: #999; background-color: #000; }
*/
        if (typeof data.link === 'string') {
            if (data.link == 'INIT') {
                $link_stat.css({ "color": "#333",
                      "background-color": "#FF0" });
            } else if (data.link == 'UP') {
                $link_stat.css({ "color": "#FFF",
                      "background-color": "#0C0" });
            } else if (data.link == 'DOWN') {
                $link_stat.css({ "color": "#000",
                      "background-color": "#F00" });
            } else {
                $link_stat.css({ "color": "#666",
                      "background-color": "#CCC" });
            }
            $link_stat.text(data.link);
        }
        if (typeof data.host === 'string') {
            $host.text(' ('+data.host+')');
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
        ait = $outbound.val() + '\n';
        $outbound.val('');
    });

    $('#debug').click(function (e) {
        $raw_data.toggleClass('hidden');
    });

    startRefresh();
});
