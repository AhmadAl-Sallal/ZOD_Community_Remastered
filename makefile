CC=g++
CFLAGS=-c -g `sdl-config --cflags` -I/ucrt64/include/mariadb -D DISABLE_REGCHECK
LDFLAGS=`sdl-config --libs` -lSDL_image -lSDL_mixer -lSDL_ttf -lm -lmysqlclient -lws2_32 -liphlpapi -lopengl32
INCLUDES=
# Track direct and transitive project header dependencies with GCC.
DEPFLAGS = -MMD -MP -MF $(@:.o=.d) -MT $@
BASEFILES = zlan_lobby.cpp zstart_multiplayer.cpp zstart_replay.cpp zstart_menu.cpp zstart_editor.cpp zstart_select.cpp common.cpp zsdl.cpp event_handler.cpp socket_handler.cpp server_socket.cpp client_socket.cpp zhud.cpp zfont_engine.cpp zfont.cpp zmini_map.cpp cursor.cpp zgui_window.cpp zbuildlist.cpp zsound_engine.cpp zmusic_engine.cpp zcomp_message_engine.cpp zpath_finding.cpp zpath_finding_astar.cpp zsettings.cpp zsdl_opengl.cpp zportrait.cpp zteam.cpp ztray.cpp zmysql.cpp zpsettings.cpp ztime.cpp zvote.cpp zunitrating.cpp zencrypt_aes.cpp zolists.cpp zgui_main_menu_base.cpp zgui_main_menu_widget.cpp
COREFILES = zcore.cpp zserver.cpp zserver_events.cpp zserver_commands.cpp zclient.cpp zplayer.cpp zplayer_events.cpp zbot.cpp zbot_events.cpp SDL_rotozoom.cpp
EFFECTFILES = ebullet.cpp elaser.cpp eflame.cpp epyrofire.cpp etoughrocket.cpp etoughmushroom.cpp etoughsmoke.cpp elightrocket.cpp elightinitfire.cpp emomissilerockets.cpp emissilecrockets.cpp erobotdeath.cpp edeath.cpp estandard.cpp edeathsparks.cpp eturrentmissile.cpp esideexplosion.cpp ecannondeath.cpp eunitparticle.cpp erockparticle.cpp erockturrent.cpp emapobjectturrent.cpp ebridgeturrent.cpp ecraneconco.cpp erobotturrent.cpp etrack.cpp etankdirt.cpp etanksmoke.cpp etankoil.cpp etankspark.cpp
OBJECTFILES = zmap.cpp zmap_crater_graphics.cpp zeffect.cpp zobject.cpp zrobot.cpp zbuilding.cpp zcannon.cpp zvehicle.cpp oflag.cpp orock.cpp ogrenades.cpp orockets.cpp ohut.cpp omapobject.cpp abird.cpp ahutanimal.cpp
ROBOTFILES = rgrunt.cpp rpsycho.cpp rtough.cpp rsniper.cpp rpyro.cpp rlaser.cpp
BUILDINGFILES = bfort.cpp brobot.cpp bvehicle.cpp bradar.cpp brepair.cpp bbridge.cpp
VEHICLEFILES = vjeep.cpp vlight.cpp vmedium.cpp vheavy.cpp vmissilelauncher.cpp vapc.cpp vcrane.cpp
CANNONFILES = cgatling.cpp cgun.cpp chowitzer.cpp cmissilecannon.cpp
GUIFILES = gwproduction.cpp gwproduction_fus.cpp gwproduction_us.cpp gwlogin.cpp gwcreateuser.cpp gwfactory_list.cpp gmm_change_teams.cpp gmm_main_menu.cpp gmm_manage_bots.cpp gmm_alliances.cpp gmm_player_list.cpp gmm_select_map.cpp gmm_options.cpp gmm_warning.cpp gmm_multiplayer.cpp gmmw_button.cpp gmmw_label.cpp gmmw_list.cpp gmmw_team_color.cpp gmmw_radio.cpp gmmw_textbox.cpp
CPPFILES = $(BASEFILES) $(COREFILES) $(EFFECTFILES) $(OBJECTFILES) $(ROBOTFILES) $(BUILDINGFILES) $(VEHICLEFILES) $(CANNONFILES) $(GUIFILES)
EXENAME = zod
# Embed a SECOND compilation of map_editor.cpp in the main EXE.
# Standalone editor keeps its own independent map_editor.o/main().
OFILES = main.o map_editor_embedded.o $(CPPFILES:.cpp=.o)
MAPEDITOR_NAME = zod_map_editor
MAPEDITOR_OFILES = map_editor.o $(CPPFILES:.cpp=.o)
DATE = `date +%m-%d-%y`
DEPFILES = $(sort $(OFILES:.o=.d) $(MAPEDITOR_OFILES:.o=.d))
.DEFAULT_GOAL := main
.PHONY: main map_editor clean clean_map_editor zip assets


main: $(OFILES) 
	$(CC) -o $(EXENAME) $(OFILES) $(LDFLAGS)
	
map_editor: $(MAPEDITOR_OFILES) 
	$(CC) -o $(MAPEDITOR_NAME) $(MAPEDITOR_OFILES) $(LDFLAGS)

#base files -----------------------------------

map_editor.o: map_editor.cpp
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) map_editor.cpp

map_editor_embedded.o: map_editor.cpp zod_editor_embedded.h
	$(CC) $(CFLAGS) -DZOD_EDITOR_EMBEDDED $(INCLUDES) $(DEPFLAGS) -o $@ map_editor.cpp

main.o: main.cpp main.h zod_editor_embedded.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) main.cpp
	
common.o: common.cpp common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) common.cpp
	
zsdl.o: zsdl.cpp zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zsdl.cpp
	
zsdl_opengl.o: zsdl_opengl.cpp zsdl_opengl.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zsdl_opengl.cpp

event_handler.o: event_handler.cpp event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) event_handler.cpp

socket_handler.o: socket_handler.cpp socket_handler.h constants.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) socket_handler.cpp

server_socket.o: server_socket.cpp server_socket.h socket_handler.h constants.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) server_socket.cpp

client_socket.o: client_socket.cpp client_socket.h socket_handler.h constants.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) client_socket.cpp
	
zhud.o: zhud.cpp zhud.h zsdl.h zobject.h zmini_map.h zfont_engine.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zhud.cpp
	
zfont_engine.o: zfont_engine.cpp zfont_engine.h zfont.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zfont_engine.cpp
	
zfont.o: zfont.cpp zfont.h zfont.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zfont.cpp
	
zmini_map.o: zmini_map.cpp zmini_map.h zmap.h zsdl.h zobject.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zmini_map.cpp

cursor.o: cursor.cpp cursor.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) cursor.cpp
	
zgui_window.o: zgui_window.cpp zgui_window.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zgui_window.cpp
	
zbuildlist.o: zbuildlist.cpp zbuildlist.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zbuildlist.cpp

zsound_engine.o: zsound_engine.cpp zsound_engine.h constants.h zsdl.h zmap.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zsound_engine.cpp

zmusic_engine.o: zmusic_engine.cpp zmusic_engine.h constants.h zsdl.h zmap.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zmusic_engine.cpp
	
zcomp_message_engine.o: zcomp_message_engine.cpp zcomp_message_engine.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zcomp_message_engine.cpp
	
zpath_finding.o: zpath_finding.cpp zpath_finding.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zpath_finding.cpp
	
zpath_finding_astar.o: zpath_finding_astar.cpp zpath_finding_astar.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zpath_finding_astar.cpp
	
zsettings.o: zsettings.cpp zsettings.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zsettings.cpp
	
zportrait.o: zportrait.cpp zportrait.h constants.h zsdl.h zsdl_opengl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zportrait.cpp
	
zteam.o: zteam.cpp zteam.h constants.h zsdl.h zsdl_opengl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zteam.cpp

zmysql.o: zmysql.cpp zmysql.h constants.h common.h zpsettings.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zmysql.cpp
	
zpsettings.o: zpsettings.cpp zpsettings.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zpsettings.cpp

ztime.o: ztime.cpp ztime.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ztime.cpp
	
zvote.o: zvote.cpp zvote.h zmap.h zfont_engine.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zvote.cpp
	
zunitrating.o: zunitrating.cpp zunitrating.h zmap.h constants.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zunitrating.cpp
	
zencrypt_aes.o: zencrypt_aes.cpp zencrypt_aes.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zencrypt_aes.cpp
	
zolists.o: zolists.cpp zolists.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zolists.cpp
	
zgui_main_menu_base.o: zgui_main_menu_base.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zgui_main_menu_base.cpp
	
zgui_main_menu_widget.o: zgui_main_menu_widget.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zgui_main_menu_widget.cpp
	
#core files -----------------------------------

zcore.o: zcore.cpp zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zcore.cpp
	
zserver.o: zserver.cpp zserver.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zserver.cpp
	
zserver_events.o: zserver_events.cpp zserver.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zserver_events.cpp
	
zserver_commands.o: zserver_commands.cpp zserver.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zserver_commands.cpp
	
zclient.o: zclient.cpp zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zclient.cpp
	
zplayer.o: zplayer.cpp zplayer.h zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zplayer.cpp
	
zplayer_events.o: zplayer_events.cpp zplayer.h zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zplayer_events.cpp
	
zbot.o: zbot.cpp zbot.h zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zbot.cpp
	
zbot_events.o: zbot_events.cpp zbot.h zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zbot_events.cpp
	
ztray.o: ztray.cpp ztray.h zclient.h zcore.h event_handler.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ztray.cpp
	
SDL_rotozoom.o: SDL_rotozoom.cpp SDL_rotozoom.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) SDL_rotozoom.cpp
	
#effect files ---------------------------------
	
ebullet.o: ebullet.cpp ebullet.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ebullet.cpp
	
elaser.o: elaser.cpp elaser.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) elaser.cpp
	
eflame.o: eflame.cpp eflame.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) eflame.cpp
	
epyrofire.o: epyrofire.cpp epyrofire.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) epyrofire.cpp
	
etoughrocket.o: etoughrocket.cpp etoughrocket.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etoughrocket.cpp
	
etoughmushroom.o: etoughmushroom.cpp etoughmushroom.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etoughmushroom.cpp
	
etoughsmoke.o: etoughsmoke.cpp etoughsmoke.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etoughsmoke.cpp
	
elightrocket.o: elightrocket.cpp elightrocket.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) elightrocket.cpp
	
elightinitfire.o: elightinitfire.cpp elightinitfire.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) elightinitfire.cpp
	
emomissilerockets.o: emomissilerockets.cpp emomissilerockets.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) emomissilerockets.cpp
	
emissilecrockets.o: emissilecrockets.cpp emissilecrockets.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) emissilecrockets.cpp
	
erobotdeath.o: erobotdeath.cpp erobotdeath.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) erobotdeath.cpp
	
eunitparticle.o: eunitparticle.cpp eunitparticle.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) eunitparticle.cpp
	
edeath.o: edeath.cpp edeath.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) edeath.cpp
	
estandard.o: estandard.cpp estandard.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) estandard.cpp
	
edeathsparks.o: edeathsparks.cpp edeathsparks.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) edeathsparks.cpp
	
eturrentmissile.o: eturrentmissile.cpp eturrentmissile.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) eturrentmissile.cpp
	
esideexplosion.o: esideexplosion.cpp esideexplosion.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) esideexplosion.cpp
	
ecannondeath.o: ecannondeath.cpp ecannondeath.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ecannondeath.cpp
	
erockparticle.o: erockparticle.cpp erockparticle.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) erockparticle.cpp

erockturrent.o: erockturrent.cpp erockturrent.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) erockturrent.cpp
	
emapobjectturrent.o: emapobjectturrent.cpp emapobjectturrent.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) emapobjectturrent.cpp
	
ebridgeturrent.o: ebridgeturrent.cpp ebridgeturrent.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ebridgeturrent.cpp
	
ecraneconco.o: ecraneconco.cpp ecraneconco.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ecraneconco.cpp
	
erobotturrent.o: erobotturrent.cpp erobotturrent.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) erobotturrent.cpp
	
etrack.o: etrack.cpp etrack.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etrack.cpp
	
etankdirt.o: etankdirt.cpp etankdirt.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etankdirt.cpp
	
etanksmoke.o: etanksmoke.cpp etanksmoke.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etanksmoke.cpp
	
etankoil.o: etankoil.cpp etankoil.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etankoil.cpp
	
etankspark.o: etankspark.cpp etankspark.h zsdl.h zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) etankspark.cpp
	
#robot files ----------------------------------

rgrunt.o: rgrunt.cpp rgrunt.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rgrunt.cpp
	
rpsycho.o: rpsycho.cpp rpsycho.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rpsycho.cpp
	
rtough.o: rtough.cpp rtough.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rtough.cpp
	
rsniper.o: rsniper.cpp rsniper.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rsniper.cpp
	
rpyro.o: rpyro.cpp rpyro.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rpyro.cpp
	
rlaser.o: rlaser.cpp rlaser.h zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) rlaser.cpp

#vehicle files --------------------------------

vjeep.o: vjeep.cpp vjeep.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vjeep.cpp
	
vlight.o: vlight.cpp vlight.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vlight.cpp
	
vmedium.o: vmedium.cpp vmedium.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vmedium.cpp
	
vheavy.o: vheavy.cpp vheavy.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vheavy.cpp
	
vmissilelauncher.o: vmissilelauncher.cpp vmissilelauncher.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vmissilelauncher.cpp
	
vapc.o: vapc.cpp vapc.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vapc.cpp
	
vcrane.o: vcrane.cpp vcrane.h zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) vcrane.cpp

#cannon files ---------------------------------

cgatling.o: cgatling.cpp cgatling.h zcannon.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) cgatling.cpp
	
cgun.o: cgun.cpp cgun.h zcannon.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) cgun.cpp
	
chowitzer.o: chowitzer.cpp chowitzer.h zcannon.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) chowitzer.cpp
	
cmissilecannon.o: cmissilecannon.cpp cmissilecannon.h zcannon.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) cmissilecannon.cpp

#building files -------------------------------

bfort.o: bfort.cpp bfort.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) bfort.cpp
	
brobot.o: brobot.cpp brobot.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) brobot.cpp
	
bvehicle.o: bvehicle.cpp bvehicle.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) bvehicle.cpp
	
bradar.o: bradar.cpp bradar.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) bradar.cpp
	
brepair.o: brepair.cpp brepair.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) brepair.cpp
	
bbridge.o: bbridge.cpp bbridge.h zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) bbridge.cpp

#object files ---------------------------------

zmap.o: zmap.cpp zmap.h constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zmap.cpp
	
zmap_crater_graphics.o: zmap_crater_graphics.cpp constants.h zsdl.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zmap_crater_graphics.cpp
	
zeffect.o: zeffect.cpp zeffect.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zeffect.cpp
	
zobject.o: zobject.cpp zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zobject.cpp
	
zrobot.o: zrobot.cpp zrobot.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zrobot.cpp
	
zbuilding.o: zbuilding.cpp zbuilding.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zbuilding.cpp
	
zcannon.o: zcannon.cpp zcannon.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zcannon.cpp

zvehicle.o: zvehicle.cpp zvehicle.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zvehicle.cpp

oflag.o: oflag.cpp oflag.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) oflag.cpp
	
orock.o: orock.cpp orock.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) orock.cpp
	
ogrenades.o: ogrenades.cpp ogrenades.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ogrenades.cpp
	
orockets.o: orockets.cpp orockets.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) orockets.cpp
	
ohut.o: ohut.cpp ohut.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ohut.cpp
	
omapobject.o: omapobject.cpp omapobject.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) omapobject.cpp
	
abird.o: abird.cpp abird.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) abird.cpp
	
ahutanimal.o: ahutanimal.cpp ahutanimal.h zobject.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) ahutanimal.cpp
	
#gui files ---------------------------------

gwproduction.o: gwproduction.cpp gwproduction.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwproduction.cpp
	
gwproduction_us.o: gwproduction_us.cpp gwproduction.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwproduction_us.cpp
	
gwproduction_fus.o: gwproduction_fus.cpp gwproduction.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwproduction_fus.cpp
	
gwlogin.o: gwlogin.cpp gwlogin.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwlogin.cpp
	
gwcreateuser.o: gwcreateuser.cpp gwcreateuser.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwcreateuser.cpp
	
gwfactory_list.o: gwfactory_list.cpp gwfactory_list.h zgui_window.h constants.h zsdl.h zmap.h common.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gwfactory_list.cpp
	
gmm_change_teams.o: gmm_change_teams.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_change_teams.cpp
	
gmm_main_menu.o: gmm_main_menu.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_main_menu.cpp
	
gmm_manage_bots.o: gmm_manage_bots.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_manage_bots.cpp

gmm_alliances.o: gmm_alliances.cpp gmm_alliances.h zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_alliances.cpp
	
gmm_player_list.o: gmm_player_list.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_player_list.cpp
	
gmm_select_map.o: gmm_select_map.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_select_map.cpp
	
gmm_options.o: gmm_options.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_options.cpp
	
gmm_warning.o: gmm_warning.cpp zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_warning.cpp
	
gmm_multiplayer.o: gmm_multiplayer.cpp gmm_multiplayer.h zgui_main_menu_base.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmm_multiplayer.cpp
	
gmmw_button.o: gmmw_button.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_button.cpp

gmmw_label.o: gmmw_label.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_label.cpp
	
gmmw_list.o: gmmw_list.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_list.cpp
	
gmmw_radio.o: gmmw_radio.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_radio.cpp
	
gmmw_textbox.o: gmmw_textbox.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_textbox.cpp

gmmw_team_color.o: gmmw_team_color.cpp zgui_main_menu_widgets.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) gmmw_team_color.cpp
	
zip:
	zip zod_src_${DATE}.zip *.cpp *.h makefile
	
assets:
	tar -cvjf assets_${DATE}.tar.bz2 assets/

clean:
	rm -f $(EXENAME) $(EXENAME).exe $(OFILES) $(OFILES:.o=.d)
	
clean_map_editor:
	rm -f $(MAPEDITOR_NAME) $(MAPEDITOR_NAME).exe $(MAPEDITOR_OFILES) $(MAPEDITOR_OFILES:.o=.d)



# ZOD Start Menu
zstart_menu.o: zstart_menu.cpp zstart_menu.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zstart_menu.cpp
# ZOD Map Editor Menu
zstart_editor.o: zstart_editor.cpp zstart_editor.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zstart_editor.cpp

zstart_select.o: zstart_select.cpp zstart_select.h
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) zstart_select.cpp

# These sources previously used an implicit rule without the project flags.
zlan_lobby.o zstart_multiplayer.o zstart_replay.o: %.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) $(DEPFLAGS) $<

# Missing .d files are expected on the very first build or after cleaning.
-include $(DEPFILES)
