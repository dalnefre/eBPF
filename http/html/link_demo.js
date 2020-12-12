$(function () {
    let $host = $('#host');
    let $play_pause = $('#play_pause');
    let $spinner = $('#spinner');
    let $fast_hand = $('#fast_hand');
    let $slow_hand = $('#slow_hand');
    let $link_stat = $('#link_stat');
    let $pkt_num = $('#pkt_num');
    let $inbound = $('#inbound');
    let $outbound = $('#outbound');
    let $send = $('#send');
    let $raw_data = $('#raw_data');

    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) {
            $raw_data.addClass('error');
            return;  // prevent overlapping requests
        }
        waiting = true;
        $raw_data.removeClass('error');
        var params = {};
        if ($send.prop('disabled')) {
            if ($outbound.val()) {
                params.ait = $outbound.val().slice(0, 8);  // 64-bits at a time
            } else {
                params.ait = '\n';  // add newline between outbound messages
                $send.prop('disabled', false);
            }
        }
        $.getJSON('/ebpf_map/ait.json', params)
            .done(update);
    };
    let update = function (data) {
        if (data.ait_map[1].n !== -1) {
            // inbound AIT
            var s = get_ait(data.ait_map[1].s);
            let i = s.indexOf('\u0000');
            if (i < 0) {
                i = s.length;
            }
            s = s.slice(0, i);
            //$inbound.text($inbound.text() + s);
            $inbound.append(document.createTextNode(s));
        }
        if (typeof data.sent === 'string') {
            var out = $outbound.val();
            if (out.startsWith(data.sent)) {
                out = out.slice(data.sent.length);
                $outbound.val(out);
            }
        }
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
            } else if (data.link == 'DEAD') {
                $link_stat.css({ "color": "#999",
                      "background-color": "#000" });
            } else {
                $link_stat.css({ "color": "#666",
                      "background-color": "#CCC" });
            }
            $link_stat.text(data.link);
        }
        if (typeof data.host === 'string') {
            $host.text(' ('+data.host+')');
        }
        var cnt = data.ait_map[3];
        $pkt_num.val(cnt.n & 0xFFFF);
        var fast_rot = (((cnt.b[1] << 8) | cnt.b[0]) * 360) >> 16;
        $fast_hand.attr('transform', 'rotate(' + fast_rot + ')');
//        var slow_rot = (cnt.b[2] * 360) >> 8;
        var slow_rot = (((cnt.b[2] << 8) | cnt.b[1]) * 360) >> 12;
        $slow_hand.attr('transform', 'rotate(' + slow_rot + ')');
        $raw_data.text(JSON.stringify(data, null, 2));
        waiting = false;
    };

    var animation;
    let animate = function (timestamp) {
        refresh();
        animation = requestAnimationFrame(animate);
    };
    let toggleRefresh = function () {
        if (animation) {
            cancelAnimationFrame(animation);
            animation = undefined;
            $('#pause').hide();
            $('#play').show();
        } else {
            $('#play').hide();
            $('#pause').show();
            animate();
        }
    };

    $play_pause.click(function (e) {
        toggleRefresh();
    });

    $send.click(function (e) {
        $send.prop('disabled', true);
    });

    $('#debug').click(function (e) {
        $raw_data.toggleClass('hidden');
    });

    animate();
});
