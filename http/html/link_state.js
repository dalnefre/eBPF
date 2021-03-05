$(function () {
    let $host = $('#host');
    let $play_pause = $('#play_pause');
    let $refresh_rate = $('#refresh_rate');
    let $spinner = $('#spinner');
    let $fast_hand = $('#fast_hand');
    let $link_stat = $('#link_stat');
    let $pkt_num = $('#pkt_num');
    let $inbound = $('#inbound');
    let $outbound = $('#outbound');
    let $src = $('#src');
    let $snk = $('#snk');
    let $send = $('#send');
    let $raw_data = $('#raw_data');

    var link = {  // port/link interface
        snk: {
            get full() {
                if (link.data) {
                    return link.data.link_state.link_flags.FULL;
                }
                return true;
            },
            set valid(set) {
                if (link.data) {
                    if (set && !this.full) {
                        link.data.link_state.user_flags.VALD = true;
                    }
                    if (!set && this.full) {
                        link.data.link_state.user_flags.VALD = false;
                    }
                }
            },
            get valid() {
                if (link.data) {
                    return link.data.link_state.user_flags.VALD;
                }
                return false;
            },
            set data(payload) {
                if (link.data && !this.full) {
                    link.data.link_state.outbound = payload;
                }
            },
            get data() {
                if (link.data) {
                    return link.data.link_state.outbound;
                }
            }
        },
        src: {
            get valid() {
                if (link.data) {
                    return link.data.link_state.link_flags.VALD;
                }
                return false;
            },
            set full(set) {
                if (link.data) {
                    if (set && this.valid) {
                        link.data.link_state.user_flags.FULL = true;
                    }
                    if (!set && !this.valid) {
                        link.data.link_state.user_flags.FULL = false;
                    }
                }
            },
            get full() {
                if (link.data) {
                    return link.data.link_state.user_flags.FULL;
                }
                return false;
            },
            get data() {
                if (link.data && this.valid) {
                    return link.data.link_state.inbound;
                }
            }
        }
    };

    var waiting = false;  // waiting for server response
    let refresh = function () {
        if (waiting) {
            $raw_data.addClass('error');
            return;  // prevent overlapping requests
        }
        waiting = true;
        $raw_data.removeClass('error');
        var params = {
            FULL: link.src.full,
            VALD: link.snk.valid
        };
        if (params.VALD) {
            params.DATA = link.snk.data;
        }
        $.getJSON('/link_map/link.json', params)
            .fail(jqXHRfail)
            .done(update);
    };
    let jqXHRfail = function (jqXHR, textStatus, errorThrown) {
       console.log('jqXHRfail!', textStatus, errorThrown);
    };
    let update = function (data) {
        $raw_data.text(JSON.stringify(data, null, 2));
        link.data = data;
        if (link.src.valid && !link.src.full) {  // inbound AIT
            var s = link.src.data;
            if ((s.charCodeAt(0) == 0x08)    // raw octets
            &&  (s.charCodeAt(1) > 0x80)) {  // smol length
                let n = s.charCodeAt(1) - 0x80;
                s = s.slice(2, 2 + n);
                //$inbound.text($inbound.text() + s);
                $inbound.append(document.createTextNode(s));
            }
            link.src.full = true;
        } else if (link.src.full && !link.src.valid) {
            link.src.full = false;  // clear inbound full flag
        }
        if (!link.snk.full && !link.snk.valid) {  // outbound AIT
            if ($send.prop('disabled')) {
                var out = $outbound.val();
                if ((typeof out === 'string') && (out.length > 0)) {
                    // there is data ready to send...
                    $outbound.val(out.slice(1));  // remove first character
                    out = String.fromCodePoint(0x08)  // raw octets
                        + String.fromCodePoint(0x80 + 1)  // length = 1
                        + out.charAt(0);  // first character of output
                    link.snk.data = out;
                    link.snk.valid = true;
                } else {
                    $outbound.val('');  // clear outbound
                    $send.prop('disabled', false);  // enable text field
                }
            }
        } else if (link.snk.valid && link.snk.full) {
            link.snk.valid = false;  // clear outbound valid flag
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
        $src.text(' ('+link.src.valid+','+link.src.full+')');
        $snk.text(' ('+link.snk.valid+','+link.snk.full+')');
        var seq32 = data.link_state.seq;
        $pkt_num.val(('00000000' + seq32.toString(16)).substr(-8));
        var seq16 = seq32 & 0xFFFF;
//        $pkt_num.val(('0000' + seq16.toString(16)).substr(-4));
        var fast_rot = (seq16 * 360) >> 16;
        $fast_hand.attr('transform', 'rotate(' + fast_rot + ')');
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

    $send.click(function (e) {
        $send.prop('disabled', true);
        var out = $outbound.val();
        $outbound.val(out + '\n');  // add newline to outbound message
    });

    $('#debug').click(function (e) {
        $raw_data.toggleClass('hidden');
    });

    animate();
});
