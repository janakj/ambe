name     := ambe
version  := 1.0

lib_src     := ambe.pb.cc ambe.grpc.pb.cc api.cc serial.cc rpc.cc device.cc scheduler.cc packet.cc uri.cc capi.cc
lib_hdr     := api.h capi.h device.h packet.h queue.h rpc.h scheduler.h serial.h uri.h
server_src  := ambed.cc
client_src  := ambec.cc
libs        := protobuf grpc++ grpc
client_libs := sndfile

lib_name := lib$(name)
server_name := $(name)d
client_name := $(name)c

prefix ?= /usr/local/

FLAGS     = -I . -Wall -O3
CFLAGS   += $(FLAGS) -std=gnu99
CXXFLAGS += $(FLAGS) -std=c++17

LDFLAGS  += -L. -pthread -Wl,'-rpath=$$ORIGIN' -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed -ldl

alldep = Makefile
nobuild = clean

obj_dir := .obj
pic_dir := $(obj_dir)/.pic

lib_obj  := $(lib_src:.cc=.o)
alib_obj  := $(addprefix $(obj_dir)/, $(lib_obj))
solib_obj := $(addprefix $(pic_dir)/, $(lib_obj))

server_obj := $(addprefix $(obj_dir)/, $(server_src:.cc=.o))
client_obj := $(addprefix $(obj_dir)/, $(client_src:.cc=.o))

obj := $(alib_obj) $(solib_obj) $(server_obj) $(client_obj)

# The list of all dependency files to be included at the end of the Makefile
deps := $(obj:.o=.d)

# We only want to generate and include dependency files if we're building.
ifeq (,$(MAKECMDGOALS))
    build=1
else
ifneq (,$(filter-out $(nobuild),$(MAKECMDGOALS)))
    build=1
endif
endif

# Normalize the prefix. Make sure it ends with a slash and substitute
# any double slashes with just one.
override prefix := $(subst //,/,$(prefix)/)

ifeq (/usr/local/,$(prefix))
    usr :=
else
    usr := usr/
endif


# Run the rest of the configuration only if we're building
ifeq (1,$(build))

# Make sure all object directories exist before we start building.
tmp := $(shell mkdir -p $(sort $(dir $(obj))))

CPPFLAGS += $(shell pkg-config --cflags $(libs))
CPPFLAGS += $(shell pkg-config --cflags $(client_libs))
LDFLAGS  += $(shell pkg-config --libs   $(libs))
GRPC_PLUGIN ?= $(shell which grpc_cpp_plugin)

endif # ifeq (1,$(buid))


define cc-cmd
@test -d `dirname "$@"` || mkdir -p `dirname "$@"`
$(CC) -MMD -MP -MT "$@ $(@:.o=.d)" -c $(CFLAGS) $(1) -o $@ $<
endef

define cxx-cmd
@test -d `dirname "$@"` || mkdir -p `dirname "$@"`
$(CXX) -MMD -MP -MT "$@ $(@:.o=.d)" -c $(CXXFLAGS) $(1) -o $@ $<
endef


all: lib server client $(alldep)

lib: $(lib_name).a $(lib_name).so $(alldep)
client: $(client_name) $(alldep)
server: $(server_name) $(alldep)

$(obj_dir)/%.o: %.c $(alldep)
	$(call cc-cmd)

$(obj_dir)/%.o: %.cc $(alldep)
	$(call cxx-cmd)

$(pic_dir)/%.o: %.c $(alldep)
	$(call cc-cmd,-fPIC -DPIC)

$(pic_dir)/%.o: %.cc $(alldep)
	$(call cxx-cmd,-fPIC -DPIC)

$(server_name): $(lib_name).so $(server_obj) $(alldep)
	g++ -o $@ $(server_obj) $(LDFLAGS) -l$(name)

$(client_name): $(lib_name).so $(client_obj) $(alldep)
	g++ -o $@ $(client_obj) $(LDFLAGS) -l$(name) $(shell pkg-config --libs $(client_libs))

$(lib_name).a: $(alib_obj) $(alldep)
	ar rcs $@ $(alib_obj)

$(lib_name).so: $(solib_obj) $(alldep)
	g++ -o $@ $(LDFLAGS) -shared -Wl,-soname,$@ $(solib_obj)

.PRECIOUS: %.grpc.pb.cc
%.grpc.pb.cc: %.proto $(alldep)
	protoc -I . --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_PLUGIN) $<

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto $(alldep)
	protoc -I . --cpp_out=. $<

.PHONY:
clean: $(alldep)
	rm -rf .obj *.pb.cc *.pb.h $(lib_name).so $(lib_name).a $(server_name) $(client_name)


$(DESTDIR)$(prefix)$(usr)sbin          \
$(DESTDIR)$(prefix)$(usr)lib           \
$(DESTDIR)$(prefix)$(usr)lib/pkgconfig \
$(DESTDIR)$(prefix)$(usr)include/$(name):
	install -d "$@"

install: install-libs install-server install-client $(alldep)

install-libs: install-hdr install-alib install-solib install-pc $(alldep)

install-hdr: $(DESTDIR)$(prefix)$(usr)include/$(name) $(lib_hdr) $(alldep)
	install -m 444 $(lib_hdr) "$(DESTDIR)$(prefix)$(usr)include/$(name)"

install-alib: $(DESTDIR)$(prefix)$(usr)lib $(lib_name).a $(alldep)
	install $(lib_name).a "$(DESTDIR)$(prefix)$(usr)lib"

install-solib: $(DESTDIR)$(prefix)$(usr)lib $(lib_name).so $(alldep)
	install $(lib_name).so "$(DESTDIR)$(prefix)$(usr)lib"

install-pc: $(DESTDIR)$(prefix)$(usr)lib/pkgconfig $(alldep)
	@echo 'prefix=$(prefix)$(usr)' > $</$(lib_name).pc
	@echo 'exec_prefix=$${prefix}' >> $</$(lib_name).pc
	@echo 'libdir=$${exec_prefix}/lib' >> $</$(lib_name).pc
	@echo 'includedir=$${prefix}/include' >> $</$(lib_name).pc
	@echo '' >> $</$(lib_name).pc
	@echo 'Name: $(lib_name)' >> $</$(lib_name).pc
	@echo 'Description: AMBE Vocoder Support Library' >> $</$(lib_name).pc
	@echo 'Version: $(version)' >> $</$(lib_name).pc
	@echo 'Requires.private: $(libs)' >> $</$(lib_name).pc
	@echo 'Libs: -L$${libdir} -l$(name)' >> $</$(lib_name).pc
	@echo 'Libs.private: $(LDFLAGS)' >> $</$(lib_name).pc
	@echo 'Cflags: -I$${includedir}' >> $</$(lib_name).pc

install-server: $(DESTDIR)$(prefix)$(usr)sbin $(alldep) $(server_name) install-solib
	install -s $(server_name) "$(DESTDIR)$(prefix)$(usr)sbin/$(server_name)"

install-client: $(DESTDIR)$(prefix)$(usr)bin $(alldep) $(client_name) install-solib
	install -s $(client_name) "$(DESTDIR)$(prefix)$(usr)bin/$(client_name)"

ifeq (1,$(build))
-include $(deps)
endif
