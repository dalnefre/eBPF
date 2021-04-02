$(function () {
    let $host = $('#host');
    let $play_pause = $('#play_pause');
    let $refresh_rate = $('#refresh_rate');
    let $raw_data = $('#raw_data');

    let octets_to_hex = function (s) {
        var i = 0;
        var h = '';
        while (i < s.length) {
            let d = '00' + s.charCodeAt(i).toString(16);
            h += d.slice(-2);
            ++i;
        }
        return h;
    };
    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) {
            $raw_data.addClass('error');
            return;  // prevent overlapping requests
        }
        waiting = true;
        $raw_data.removeClass('error');
        var params = {};
        $.getJSON('/link_map/if_data.json', params)
            .fail(jqXHRfail)
            .done(update);
    };
    let jqXHRfail = function (jqXHR, textStatus, errorThrown) {
       console.log('jqXHRfail!', textStatus, errorThrown);
    };
    let update = function (data) {
        $raw_data.text(JSON.stringify(data, null, 2));
        if (typeof data.host === 'string') {
            $host.text(' ('+data.host+')');
            $('#cell_0 text').text(data.host);
        }
        var i = 0;
        while (i < data.if_data.length) {
            let cell = data.if_data[i];
            let elem = $('#cell_' + cell.i);
            elem.children('text').text(cell.name);
            if (cell.IFF.LOOPBACK) {
                elem.children('circle').attr('fill', '#EEE');
            } else if (cell.IFF.RUNNING) {
                elem.children('circle').attr('fill', '#CFC');
            } else if (cell.IFF.UP) {
                elem.children('circle').attr('fill', '#FCC');
            } else {
                elem.children('circle').attr('fill', '#CCC');
            }
            elem.removeClass('hidden');
            ++i;
        }
//        $fast_hand.attr('transform', 'rotate(' + fast_rot + ')');
        waiting = false;
    };

    var animation;
    var last_time = 0;
    let animate = function (timestamp) {
        let time_diff = (timestamp - last_time);
        var rate = $refresh_rate.val() * 1;
        if (!rate || (rate < 0.1) || (rate > 60)) {
            rate = 60;
        }
        let delta_t = 1000 / rate;
        if (time_diff >= delta_t) {
            last_time = timestamp;
            refresh();
        }
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

    $('#debug').click(function (e) {
        $raw_data.toggleClass('hidden');
    });

    animate();
});
