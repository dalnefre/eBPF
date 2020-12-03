$(document).ready(function () {
    $('#send').click(function (e) {
        $.getJSON('/ebpf_map/ait.json')
            .done(function (data) {
                $('#outbound').html(JSON.stringify(data, null, 2));
            });
        alert('fetching data...');
    });
});
