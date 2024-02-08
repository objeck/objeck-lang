if [%1]==[] goto end
if [%2]==[] goto end

obc json_stream
obr json_stream %1 %2
:end