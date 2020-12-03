$(function () {
    let $outbound = $('#outbound');

    $('#send').click(function (e) {
        $.getJSON('/ebpf_map/ait.json')
            .done(function (data) {
                $outbound.text(JSON.stringify(data, null, 2));
            });
    });
});
