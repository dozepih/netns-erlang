-module(sysSock).

-compile(export_all).

-define(ITEMS, 100).

%% ets:new(a,[named_table,bag]).
%% L = lists:seq(1, 20).
%% Res = [{a, I} || I <- L].
%% ets:insert(a,Res).
%%
%% gcc -fPIC -shared -o sysSock.so sock.c -I /usr/local/lib/erlang/usr/include/

init() ->
    erlang:load_nif("./sysSock", 0),
    case ets:info(sysSock) of
        undefined -> ets:new(sysSock, [bag, named_table, {keypos, 1}]);
        _ -> ok
    end.

sock(_Pid, _Items, _Key) -> not_loaded(?LINE).
closesock(_Fd) -> not_loaded(?LINE).

new_items(Key) -> new_items(Key, ?ITEMS).
new_items(Key, Items) ->
    ok = sock(self(), Items, Key),
    receive
        Msg when is_list(Msg) ->
            %io:format("got: ~p~n", [Msg]),
            L = [{Key, I} || I <- Msg],
            ets:insert(sysSock, L);
        Error ->
            Error
    end.

next_free(Key) ->
    try ets:lookup(sysSock, Key) of
        [{_Key, First}=Item|_] ->
        case First of
            [] -> [];
            _ ->
                ets:delete_object(sysSock, Item),
                First
        end;
        [] ->
            %% we should request more items before they are really emptied to
            %% minimize race-conditions in the NIF so two threds aren't
            %% requesting refill of same namespace. Perhaps a lock here can
            %% solve that, hmm..
            case new_items(Key, ?ITEMS) of
                R when is_atom(R) ->
                    %io:format("return: ~p~n", [R]),
                    R;
                _ ->
                    ok
            end,
            next_free(Key)
    catch
        Error -> {error, Error}
    end.

not_loaded(Line) ->
    exit({not_loaded, [{module, ?MODULE}, {line, Line}]}).

%% -------------------------------------
%% test connections, prerequsites:
%% nc -k -l 9898
%% ip netns add 1
test_gentcp(Connections) -> docon(Connections).
test_gentcp(Connections, Ns) -> docon(Connections, Ns).

docon(No) -> docon(No, 0).
docon(0, 0) -> ok;
docon(0, netns) -> ok;
docon(No, 0) ->
    Port = next_free(0),
    %%io:format("connecting: ~p~n", [Port]),
    {ok, S} = gen_tcp:connect("192.168.0.100", 80, [binary,
                                                {nodelay, true},
                                                {fd, Port}]),
    %%gen_tcp:send(S, erlang:integer_to_binary(Port)),
    gen_tcp:close(S),
    closesock(Port),
    docon(No - 1, 0);
docon(No, netns) ->
    Port = next_free(1),
    {ok, S} = gen_tcp:connect("192.168.0.100", 80, [binary,
                                      {nodelay, true},
                                      {fd, Port}]),
                                      %{netns, "/var/run/netns/topi"}]),
    gen_tcp:close(S),
    closesock(Port),
    docon(No - 1, netns).

