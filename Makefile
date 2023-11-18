# uncomment to add many debug messages
#DEBUG_FLAG=-DOHS_DEBUG
EXEC_NAME=openqm_httpd_server
OBJS=openqm_httpd_server.o openqm_httpd_server_config.o openqm_httpd_server_url.o
OPENQM_ROOT=/home/thierry/openqm
INCLUDES=-I$(OPENQM_ROOT)/openqm.account/SYSCOM -I$(OPENQM_ROOT)/openqm.account/gplsrc
CCFLAGS=-Wall -g
LT_LDFLAGS=$(OPENQM_ROOT)/openqm.account/bin/qmclilib64.o $(OPENQM_ROOT)/openqm.account/gplobj/match_template64.o -lmicrohttpd -lconfig -lpcre
DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

all: $(EXEC_NAME)

$(EXEC_NAME): $(OBJS)
	gcc -o $@ $(OBJS) $(LT_LDFLAGS)

%.o : %.c
%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	gcc $(CCFLAGS) $(DEPFLAGS) $(INCLUDES) $(DEBUG_FLAG) -c $< -o $@

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(OBJS:%.o=$(DEPDIR)/%.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

clean:
	-rm -f $(OBJS) openqm_httpd_server
