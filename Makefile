EXEC_NAME=openqm_httpd_server
OBJS=openqm_httpd_server.o openqm_httpd_server_config.o openqm_httpd_server_url.o
INCLUDES=-I/home/openqm/openqm.account/SYSCOM -I/home/openqm/openqm.account/gplsrc
CCFLAGS=-Wall -g
LT_LDFLAGS=/home/openqm/openqm.account/bin/qmclilib64.o /home/openqm/openqm.account/gplobj/match_template64.o -lmicrohttpd -lconfig -lpcre
DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

all: $(EXEC_NAME)

$(EXEC_NAME): $(OBJS)
	gcc -o $@ $(OBJS) $(LT_LDFLAGS)

%.o : %.c
%.o : %.c $(DEPDIR)/%.d | $(DEPDIR)
	gcc $(CCFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(OBJS:%.o=$(DEPDIR)/%.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

clean:
	-rm -f $(OBJS) openqm_httpd_server
