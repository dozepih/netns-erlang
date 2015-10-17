ERL ?= erl
APP := netns
REBAR := ./rebar

.PHONY: deps

all: deps
	@$(REBAR) compile

deps: $(REBAR)
	@$(REBAR) get-deps

clean: $(REBAR)
	@$(REBAR) clean

distclean: $(REBAR) clean
	@$(REBAR) delete-deps

test: $(REBAR)
	@$(REBAR) ct

docs:
	@erl -noshell -run edoc_run application '$(APP)' '"."' '[]'

$(REBAR):
	wget --output-document=$(REBAR) http://cloud.github.com/downloads/basho/rebar/rebar && chmod u+x $(REBAR)
