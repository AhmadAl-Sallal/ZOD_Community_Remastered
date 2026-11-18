#include <random>
#include "zsound_engine.h"
#include "common.h"

using namespace COMMON;

bool ZSoundEngine::finished_init = NULL;
ZMap *ZSoundEngine::zmap = NULL;
ZSound ZSoundEngine::engine_sound[MAX_ENGINE_SOUNDS];

ZSound ZSoundEngine::rifle_fire;
ZSound ZSoundEngine::psycho_fire;
ZSound ZSoundEngine::tough_fire;
ZSound ZSoundEngine::pyro_fire;
ZSound ZSoundEngine::laser_fire;
ZSound ZSoundEngine::gun_fire;
ZSound ZSoundEngine::gatling_fire;
ZSound ZSoundEngine::jeep_fire;
ZSound ZSoundEngine::light_fire;
ZSound ZSoundEngine::medium_fire;
ZSound ZSoundEngine::heavy_fire;
ZSound ZSoundEngine::momissile_fire;
ZSound ZSoundEngine::ricochet;
ZSound ZSoundEngine::radar;

ZSound ZSoundEngine::explosion[5];
ZSound ZSoundEngine::turrent_explosion;
ZSound ZSoundEngine::comp_vehicle;
ZSound ZSoundEngine::comp_robot;
ZSound ZSoundEngine::comp_gun;
ZSound ZSoundEngine::comp_starting_manufacture;
ZSound ZSoundEngine::comp_manufacturing_canceled;
ZSound ZSoundEngine::comp_starting_repair;
ZSound ZSoundEngine::comp_vehicle_repaired;
ZSound ZSoundEngine::comp_territory_lost;
ZSound ZSoundEngine::comp_radar_activated;
ZSound ZSoundEngine::comp_fort_under_attack;
ZSound ZSoundEngine::ambience[MAX_PLANET_TYPES][5];
ZSound ZSoundEngine::jackhammer;

namespace {
    const int voice_channel = 0, ambience_channel = 1, jack_channel = 2;
    Mix_Chunk *button_click = NULL;
    const int reserved_channels = 3;
    bool battle_audio = false, result_audio = false, result_won = false;
    bool jack_visible = false, ambient_paused = false;
    int last_ambient = -1, ambient_planet = -1, last_win = -1, last_loss = -1;
    double next_ambient = 0, next_result_voice = 0;
    std::mt19937 audio_rng(0x5a4f4426u);
    int Pick(int n) { return std::uniform_int_distribution<int>(0, n-1)(audio_rng); }
    const int ambient_count[MAX_PLANET_TYPES] = {5,4,5,4,4};
    const char *ambient_paths[MAX_PLANET_TYPES][5] = {
        {"Desert/BUNNY2.wav","Desert/BUNNY3.wav","Desert/DESBIRD.wav","Desert/HBCRCKLP.wav","Desert/RATTLEXX.wav"},
        {"Volcanic/BEAST4.wav","Volcanic/BEAST6.wav","Volcanic/BEAST7.wav","Volcanic/VBIRD1.wav",NULL},
        {"Arctic/PINGU1.wav","Arctic/PINGU2.wav","Arctic/PINGU3.wav","Arctic/SEAL1.wav","Arctic/SEAL2.wav"},
        {"Jungle/CHIMP2.wav","Jungle/JNGBIRD1.wav","Jungle/JNGBIRD2.wav","Jungle/MONKEY.wav",NULL},
        {"City/BATCHIRP.wav","City/CROW2.wav","City/HOWL.wav","City/OWL2.wav",NULL}
    };
    // Length after SDL_mixer has converted the chunk to the actual output format.
    double ChunkSeconds(Mix_Chunk *chunk)
    {
        int rate=0, channels=0; Uint16 format=0;
        if(!chunk || !Mix_QuerySpec(&rate,&format,&channels)) return 0;
        const int bytes_per_second = rate * channels * ((format & 0xff) / 8);
        return bytes_per_second > 0 ? double(chunk->alen)/bytes_per_second : 0;
    }
}

ZSound::ZSound()
{
	snd_chunk = NULL;
	next_play_time = 0;
	repeat_channel = -1;
}

void ZSound::LoadSound(string filename, int base_volume_, int volume_shift_, double play_time_shift_)
{
	base_volume = base_volume_;
	volume_shift = volume_shift_;
	play_time_shift = play_time_shift_;
	snd_chunk = MIX_Load_Error(filename);

	if(snd_chunk)
		snd_chunk->volume = base_volume;
}

void ZSound::PlaySound()
{
	double the_time;

	if(!snd_chunk) return;

	the_time = current_time();

	if(the_time < next_play_time) return;

	next_play_time = the_time + play_time_shift + 0.01 * (rand() % 31);

	//add some uniqueness to each sound
	snd_chunk->volume = base_volume + (rand() % volume_shift);

	ZMix_PlayChannel(-1, snd_chunk, 0);
}

void ZSound::RepeatSound()
{
	int i;

	if(!snd_chunk) return;

	//we already repeating?
	if(repeat_channel != -1) return;

	//find a channel
	for(i=reserved_channels;i<ZSOUND_MIX_CHANNELS;i++)
		if(!Mix_Playing(i))
			break;

	//find one?
	if(i==ZSOUND_MIX_CHANNELS) return;

	//set and play
	repeat_channel = i;

	if(ZMix_PlayChannel(repeat_channel, snd_chunk, -1) < 0 || !Mix_Playing(repeat_channel))
		repeat_channel = -1;

}

void ZSound::StopRepeatSound()
{
	//are we repeating?
	if(repeat_channel == -1) return;

	Mix_HaltChannel(repeat_channel);

	repeat_channel = -1;
}

ZSoundEngine::ZSoundEngine()
{
	zmap = NULL;
}

void ZSoundEngine::Shutdown()
{
    finished_init = false;
    Mix_HaltChannel(-1);
    if(button_click) Mix_FreeChunk(button_click);
    button_click = NULL;
    ZSound *sounds[] = { &rifle_fire, &psycho_fire, &tough_fire, &pyro_fire, &laser_fire, &gun_fire, &gatling_fire, &jeep_fire, &light_fire, &medium_fire, &heavy_fire, &momissile_fire, &ricochet, &radar, &turrent_explosion, &comp_vehicle, &comp_robot, &comp_gun, &comp_starting_manufacture, &comp_manufacturing_canceled, &comp_starting_repair, &comp_vehicle_repaired, &comp_territory_lost, &comp_radar_activated, &comp_fort_under_attack, &jackhammer };
    for(size_t i=0;i<sizeof(sounds)/sizeof(sounds[0]);i++)
    {
        ZSound *s = sounds[i];
        if(s->snd_chunk) Mix_FreeChunk(s->snd_chunk);
        s->snd_chunk = NULL; s->repeat_channel = -1; s->next_play_time = 0;
    }
    for(int i=0;i<5;i++)
    {
        if(explosion[i].snd_chunk) Mix_FreeChunk(explosion[i].snd_chunk);
        explosion[i].snd_chunk = NULL; explosion[i].repeat_channel = -1; explosion[i].next_play_time = 0;
    }
    for(int i=0;i<MAX_ENGINE_SOUNDS;i++)
    {
        if(engine_sound[i].snd_chunk) Mix_FreeChunk(engine_sound[i].snd_chunk);
        engine_sound[i].snd_chunk = NULL; engine_sound[i].repeat_channel = -1; engine_sound[i].next_play_time = 0;
    }
    for(int planet=0;planet<MAX_PLANET_TYPES;planet++)
        for(int i=0;i<ambient_count[planet];i++)
        {
            ZSound &sound = ambience[planet][i];
            if(sound.snd_chunk) Mix_FreeChunk(sound.snd_chunk);
            sound.snd_chunk = NULL; sound.repeat_channel = -1; sound.next_play_time = 0;
        }
    battle_audio = result_audio = jack_visible = ambient_paused = false;
    ambient_planet = last_ambient = -1;
    next_ambient = 0;
    zmap = NULL;
}

void ZSoundEngine::Init(ZMap *zmap_)
{
	char filename_c[500];
	int i;

	zmap = zmap_;

	//this may not be the best place for this
    const int saved_volume = Mix_Volume(0, -1);
	Mix_AllocateChannels(ZSOUND_MIX_CHANNELS);
    Mix_ReserveChannels(reserved_channels);
    if(saved_volume >= 0) Mix_Volume(-1, saved_volume);
    battle_audio = result_audio = ambient_paused = false;
    ambient_planet = last_ambient = -1;
    next_ambient = 0;
    audio_rng.seed(SDL_GetTicks());
    button_click = Mix_LoadWAV("assets/sounds/CLICK1L.wav");
    if(button_click) Mix_VolumeChunk(button_click, 75);

	for(i=0;i<5;i++)
	{
		sprintf(filename_c, "assets/sounds/explosion_%02d.wav", i);
		explosion[i].LoadSound(filename_c, 30);
	}

	psycho_fire.LoadSound("assets/sounds/MACHGUN2.wav");
	ricochet.LoadSound("assets/sounds/RICOCH1.wav");
	jeep_fire.LoadSound("assets/sounds/JEEPMGUN.wav", 40, 20, 0.15);
	light_fire.LoadSound("assets/sounds/LTANKGUN.wav", 25);
	medium_fire.LoadSound("assets/sounds/MTANKGUN.wav", 25);
	heavy_fire.LoadSound("assets/sounds/HTANKGUN.wav", 25);
	momissile_fire.LoadSound("assets/sounds/MOBIMIS2.wav");
	rifle_fire.LoadSound("assets/sounds/RIFLE3.wav", 10, 10);
	tough_fire.LoadSound("assets/sounds/MOBIMISS.wav");
	pyro_fire.LoadSound("assets/sounds/FLAMER.wav", 20, 10);
	laser_fire.LoadSound("assets/sounds/LASERGUN.wav", 20, 10);
	gun_fire.LoadSound("assets/sounds/LTGUN.wav");
	gatling_fire.LoadSound("assets/sounds/GATTGUN.wav");
	turrent_explosion.LoadSound("assets/sounds/METGRND.wav");

    for(int planet=0;planet<MAX_PLANET_TYPES;planet++)
        for(int a=0;a<ambient_count[planet];a++)
            ambience[planet][a].LoadSound(string("assets/sounds/")+ambient_paths[planet][a], 16, 1);
    jackhammer.LoadSound("assets/sounds/JACKHMMR.wav", 28, 1);
	radar.LoadSound("assets/sounds/radar_sound.wav", 20);

	comp_vehicle.LoadSound("assets/sounds/comp_vehicle_manufactured.wav");
	comp_robot.LoadSound("assets/sounds/comp_robot_manufactured.wav");
	comp_gun.LoadSound("assets/sounds/comp_gun_manufactured.wav");
	comp_starting_manufacture.LoadSound("assets/sounds/comp_starting_manufacture.wav");
	comp_manufacturing_canceled.LoadSound("assets/sounds/comp_manufacturing_canceled.wav");
	comp_starting_repair.LoadSound("assets/sounds/comp_starting_repair.wav");
	comp_vehicle_repaired.LoadSound("assets/sounds/comp_vehicle_repaired.wav");
	comp_territory_lost.LoadSound("assets/sounds/comp_territory_lost.wav");
	comp_radar_activated.LoadSound("assets/sounds/comp_radar_activated.wav");
	comp_fort_under_attack.LoadSound("assets/sounds/comp_fort_under_attack.wav");

	for(i=0;i<MAX_COMP_LOSING_MESSAGES;i++)
	{
		sprintf(filename_c, "assets/sounds/comp_youre_losing_%02d.wav", i);
		engine_sound[i+COMP_YOUR_LOSING_0].LoadSound(filename_c);
	}

	engine_sound[THROW_GRENADE_SND].LoadSound("assets/sounds/GRENLOBX.wav");

	engine_sound[YES_SIR1_SND].LoadSound("assets/sounds/ROB01.wav");
	engine_sound[YES_SIR2_SND].LoadSound("assets/sounds/ROB02.wav");
	engine_sound[YES_SIR3_SND].LoadSound("assets/sounds/ROB03.wav");
	engine_sound[UNIT_REPORTING1_SND].LoadSound("assets/sounds/ROB04.wav");
	engine_sound[UNIT_REPORTING2_SND].LoadSound("assets/sounds/ROB05.wav");
	engine_sound[UNIT_REPORTING3_SND].LoadSound("assets/sounds/ROB06.wav");
	engine_sound[GRUNTS_REPORTING_SND].LoadSound("assets/sounds/ROB07.wav");
	engine_sound[PSYCHOS_REPORTING_SND].LoadSound("assets/sounds/ROB08.wav");
	engine_sound[SNIPERS_REPORTING_SND].LoadSound("assets/sounds/ROB09.wav");
	engine_sound[TOUGHS_REPORTING_SND].LoadSound("assets/sounds/ROB10.wav");
	engine_sound[LASERS_REPORTING_SND].LoadSound("assets/sounds/ROB11.wav");
	engine_sound[PYROS_REPORTING_SND].LoadSound("assets/sounds/ROB12.wav");
	engine_sound[WERE_ON_OUR_WAY_SND].LoadSound("assets/sounds/ROB13.wav");
	engine_sound[HERE_WE_GO_SND].LoadSound("assets/sounds/ROB14.wav");
	engine_sound[YOU_GOT_IT_SND].LoadSound("assets/sounds/ROB15.wav");
	engine_sound[MOVING_IN_SND].LoadSound("assets/sounds/ROB16.wav");
	engine_sound[OKAY_SND].LoadSound("assets/sounds/ROB17.wav");
	engine_sound[ALRIGHT_SND].LoadSound("assets/sounds/ROB18.wav");
	engine_sound[NO_PROBLEM_SND].LoadSound("assets/sounds/ROB19.wav");
	engine_sound[OVER_AND_OUT_SND].LoadSound("assets/sounds/ROB20.wav");
	engine_sound[AFFIRMATIVE_SND].LoadSound("assets/sounds/ROB21.wav");
	engine_sound[GOING_IN_SND].LoadSound("assets/sounds/ROB22.wav");
	engine_sound[LETS_DO_IT_SND].LoadSound("assets/sounds/ROB23.wav");
	engine_sound[LETS_GET_THEM_SND].LoadSound("assets/sounds/ROB24.wav");
	engine_sound[WERE_UNDER_ATTACK_SND].LoadSound("assets/sounds/ROB25.wav");
	engine_sound[I_SAID_WERE_UNDER_ATTACK_SND].LoadSound("assets/sounds/ROB26.wav");
	engine_sound[HELP_HELP_SND].LoadSound("assets/sounds/ROB27.wav");
	engine_sound[THEYRE_ALL_OVER_US_SND].LoadSound("assets/sounds/ROB28.wav");
	engine_sound[WERE_LOSING_IT_SND].LoadSound("assets/sounds/ROB29.wav");
	engine_sound[AAAHHH_SND].LoadSound("assets/sounds/ROB30.wav");
	engine_sound[OH_MY_GOD_SND].LoadSound("assets/sounds/ROB31.wav");
	engine_sound[FOR_CHRIST_SAKE_SND].LoadSound("assets/sounds/ROB32.wav");
	engine_sound[YOUR_JOKING_SND].LoadSound("assets/sounds/ROB33.wav");
	engine_sound[NO_WAY_SND].LoadSound("assets/sounds/ROB34.wav");
	engine_sound[FORGET_IT_SND].LoadSound("assets/sounds/ROB35.wav");
	engine_sound[GET_OUTTA_HERE_SND].LoadSound("assets/sounds/ROB36.wav");
	engine_sound[TARGET_DESTROYED_SND].LoadSound("assets/sounds/ROB37.wav");
	engine_sound[BRIDGE_DESTROYED_SND].LoadSound("assets/sounds/ROB38.wav");
	engine_sound[BUILDING_DESTROYED_SND].LoadSound("assets/sounds/ROB39.wav");
	engine_sound[GOOD_HIT_SND].LoadSound("assets/sounds/ROB40.wav");
	engine_sound[NICE_ONE_SND].LoadSound("assets/sounds/ROB41.wav");
	engine_sound[OH_YEAH_SND].LoadSound("assets/sounds/ROB42.wav");
	engine_sound[GOTCHA_SND].LoadSound("assets/sounds/ROB43.wav");
	engine_sound[SMOKIN_SND].LoadSound("assets/sounds/ROB44.wav");
	engine_sound[COOL_SND].LoadSound("assets/sounds/ROB45.wav");
	engine_sound[WIPE_OUT_SND].LoadSound("assets/sounds/ROB46.wav");
	engine_sound[BRIDGE_REPAIRED_SND].LoadSound("assets/sounds/ROB47.wav");
	engine_sound[BUILDING_REPAIRED_SND].LoadSound("assets/sounds/ROB48.wav");
	engine_sound[TERRITORY_TAKEN_SND].LoadSound("assets/sounds/ROB49.wav");
	engine_sound[GUN_CAPTURED_SND].LoadSound("assets/sounds/ROB51.wav");
	engine_sound[VEHICLE_CAPTURED_SND].LoadSound("assets/sounds/ROB52.wav");
	engine_sound[GRENADES_COLLECTED_SND].LoadSound("assets/sounds/ROB53.wav");
	engine_sound[LETS_TAKE_THE_FORT_SND].LoadSound("assets/sounds/ROB54.wav");
	engine_sound[LETS_DO_THEM_SND].LoadSound("assets/sounds/ROB55.wav");
	engine_sound[THEYRE_ON_THE_RUN_SND].LoadSound("assets/sounds/ROB56.wav");
	engine_sound[LETS_FINISH_THEM_OFF_SND].LoadSound("assets/sounds/ROB57.wav");
	engine_sound[WHAT_ARE_YOU_WAITING_FOR_SND].LoadSound("assets/sounds/ROB58.wav");
	engine_sound[LETS_END_IT_NOW_SND].LoadSound("assets/sounds/ROB59.wav");
	engine_sound[LETS_GO_FOR_IT_SND].LoadSound("assets/sounds/ROB60.wav");
	engine_sound[YEEHAA_END_SND].LoadSound("assets/sounds/ROB61.wav");
	engine_sound[NICE_ONE_END_SND].LoadSound("assets/sounds/ROB62.wav");
	engine_sound[ALRIGHT_END_SND].LoadSound("assets/sounds/ROB63.wav");
	engine_sound[EXCELENT_END_SND].LoadSound("assets/sounds/ROB64.wav");
	engine_sound[WEVE_DONE_IT_END_SND].LoadSound("assets/sounds/ROB65.wav");
	engine_sound[YEAHHH_END_SND].LoadSound("assets/sounds/ROB66.wav");
	engine_sound[YOURE_CRAP_LOSE_SND].LoadSound("assets/sounds/ROB67.wav");
	engine_sound[WEVE_LOST_IT_LOSE_SND].LoadSound("assets/sounds/ROB68.wav");
	engine_sound[YOUVE_BLOWN_IT_LOSE_SND].LoadSound("assets/sounds/ROB69.wav");
	engine_sound[WE_HATE_YOU_LOSE_SND].LoadSound("assets/sounds/ROB70.wav");
	engine_sound[MORON_LOSE_SND].LoadSound("assets/sounds/ROB71.wav");
	engine_sound[ASSHOLE_LOSE_SND].LoadSound("assets/sounds/ROB72.wav");
	engine_sound[ITS_OVER_LOSE_SND].LoadSound("assets/sounds/ROB73.wav");
	engine_sound[LETS_DO_IT1_START_SND].LoadSound("assets/sounds/ROB74.wav");
    engine_sound[WATER_MONSTER_SND].LoadSound("assets/sounds/City/MUNCH.wav",45,1);
    engine_sound[WATER_CROC_SND].LoadSound("assets/sounds/Jungle/CROCBITE.wav",25,1);

	finished_init = true;
}

void ZSoundEngine::RepeatWav(int snd)
{
	if(!finished_init || !battle_audio) return;

	switch(snd)
	{
	case RADAR_SND:
		radar.RepeatSound();
		break;

	}
}

void ZSoundEngine::StopRepeatWav(int snd)
{
	if(!finished_init) return;

	switch(snd)
	{
	case RADAR_SND:
		radar.StopRepeatSound();
		break;

	}
}

void ZSoundEngine::PlayWavRestricted(int snd, int x, int y, int w, int h)
{
	if(!finished_init) return;
	if(!zmap) return;

	//a check before we play
	if(!zmap->WithinView(x,y,w,h)) return;

	PlayWav(snd);
}

void ZSoundEngine::PlayWav(int snd)
{
    if(!finished_init || !battle_audio || snd < 0 || snd >= MAX_ENGINE_SOUNDS) return;
    // The result voice sequence is independent of the surviving robot queue.
    if(result_audio && ((snd >= COMP_VEHICLE_SND && snd <= COMP_YOUR_LOSING_9) ||
                        snd >= YES_SIR1_SND)) return;

	switch(snd)
	{
	default:
		engine_sound[snd].PlaySound();
		break;
	case RIFLE_FIRE_SND:
		rifle_fire.PlaySound();
		break;
	case PSYCHO_FIRE_SND:
		psycho_fire.PlaySound();
		break;
	case TOUGH_FIRE_SND:
		tough_fire.PlaySound();
		break;
	case PYRO_FIRE_SND:
		pyro_fire.PlaySound();
		break;
	case LASER_FIRE_SND:
		laser_fire.PlaySound();
		break;
	case GUN_FIRE_SND:
		gun_fire.PlaySound();
		break;
	case GATLING_FIRE_SND:
		gatling_fire.PlaySound();
		break;
	case JEEP_FIRE_SND:
		jeep_fire.PlaySound();
		break;
	case LIGHT_FIRE_SND:
		light_fire.PlaySound();
		break;
	case MEDIUM_FIRE_SND:
		medium_fire.PlaySound();
		break;
	case HEAVY_FIRE_SND:
		heavy_fire.PlaySound();
		break;
	case MOMISSILE_FIRE_SND:
		momissile_fire.PlaySound();
		break;
	case RIOCHET_SND:
		ricochet.PlaySound();
		break;
	case LIGHT_RANDOM_EXPLOSION_SND:
		explosion[rand()%2].PlaySound();
		break;
	case RANDOM_EXPLOSION_SND:
		explosion[rand()%5].PlaySound();
		break;
	case TURRENT_EXPLOSION_SND:
		turrent_explosion.PlaySound();
		break;
	case COMP_VEHICLE_SND:
		comp_vehicle.PlaySound();
		break;
	case COMP_ROBOT_SND:
		comp_robot.PlaySound();
		break;
	case COMP_GUN_SND:
		comp_gun.PlaySound();
		break;
	case COMP_STARTING_MANUFACTURE_SND:
		comp_starting_manufacture.PlaySound();
		break;
	case COMP_MANUFACTURING_CANCELED_SND:
		comp_manufacturing_canceled.PlaySound();
		break;
	case COMP_STARTING_REPAIR_SND:
		comp_starting_repair.PlaySound();
		break;
	case COMP_VEHICLE_REPAIRED_SND:
		comp_vehicle_repaired.PlaySound();
		break;
	case COMP_TERRITORY_LOST_SND:
		comp_territory_lost.PlaySound();
		break;
	case COMP_RADAR_ACTIVATED_SND:
		comp_radar_activated.PlaySound();
		break;
	case COMP_FORT_UNDER_ATTACK:
		comp_fort_under_attack.PlaySound();
		break;
	}
}

void ZSoundEngine::SetBattleAudio(bool active, bool fade)
{
    if(!finished_init) return;
    battle_audio = active;
    result_audio = false;
    jack_visible = false;
    ambient_paused = false;
    ambient_planet = last_ambient = -1;
    next_ambient = 0;
    // Forget loop channel IDs before their channels can be reused.
    radar.repeat_channel = -1;
    if(!active && fade) Mix_FadeOutChannel(-1, 500);
    else Mix_HaltChannel(-1);
}

// UI clicks must not be suppressed by the briefing/result battle-audio gate.
void ZSoundEngine::PlayButtonClick()
{
    if(finished_init && button_click) ZMix_PlayChannel(-1, button_click, 0);
}

void ZSoundEngine::StartBattleVoice()
{
    if(!finished_init || !battle_audio) return;
    Mix_Chunk *chunk = engine_sound[LETS_DO_IT1_START_SND].snd_chunk;
    if(chunk) { chunk->volume = 95; ZMix_PlayChannel(voice_channel, chunk, 0); }
}

void ZSoundEngine::BeginResult(bool won)
{
    if(!finished_init || !battle_audio) return;
    result_audio = true;
    result_won = won;
    next_result_voice = 0;
    Mix_HaltChannel(voice_channel);
    Mix_HaltChannel(ambience_channel);
    Mix_HaltChannel(jack_channel);
    // Clear existing ordinary speech so the result comments are intelligible.
    ZSound *computer[] = {&comp_vehicle,&comp_robot,&comp_gun,&comp_starting_manufacture,
        &comp_manufacturing_canceled,&comp_starting_repair,&comp_vehicle_repaired,
        &comp_territory_lost,&comp_radar_activated,&comp_fort_under_attack};
    for(int c=reserved_channels;c<ZSOUND_MIX_CHANNELS;c++)
    {
        if(!Mix_Playing(c)) continue;
        Mix_Chunk *chunk = Mix_GetChunk(c);
        if(!chunk) continue;
        bool speech = false;
        for(int i=YES_SIR1_SND;i<MAX_ENGINE_SOUNDS;i++)
            if(chunk == engine_sound[i].snd_chunk) speech = true;
        for(int i=COMP_YOUR_LOSING_0;i<=COMP_YOUR_LOSING_9;i++)
            if(chunk == engine_sound[i].snd_chunk) speech = true;
        for(size_t i=0;i<sizeof(computer)/sizeof(computer[0]);i++)
            if(chunk == computer[i]->snd_chunk) speech = true;
        if(speech) Mix_HaltChannel(c);
    }
}

void ZSoundEngine::ProcessResult(double seconds_left)
{
    if(!finished_init || !battle_audio || !result_audio || seconds_left <= 0) return;
    const double now = current_time();
    if(Mix_Playing(voice_channel) || now < next_result_voice) return;
    const int first = result_won ? YEEHAA_END_SND : YOURE_CRAP_LOSE_SND;
    const int count = result_won ? 6 : 7;
    int &last = result_won ? last_win : last_loss;
    int candidates[7], n = 0;
    for(int i=0;i<count;i++)
    {
        Mix_Chunk *chunk = engine_sound[first+i].snd_chunk;
        if(i != last && chunk && ChunkSeconds(chunk)+0.25 <= seconds_left)
            candidates[n++] = i;
    }
    if(!n) return;
    const int choice = candidates[Pick(n)];
    Mix_Chunk *chunk = engine_sound[first+choice].snd_chunk;
    chunk->volume = 115;
    ZMix_PlayChannel(voice_channel,chunk,0);
    last = choice;
    // Leave a brief gap after each complete line, never interrupt one.
    next_result_voice = now + ChunkSeconds(chunk) + 0.25;
}

void ZSoundEngine::ProcessAmbience(int planet, bool paused)
{
    if(!finished_init || !battle_audio || result_audio) return;
    if(planet<0 || planet>=MAX_PLANET_TYPES) return;
    if(paused)
    {
        if(!ambient_paused) Mix_HaltChannel(ambience_channel);
        ambient_paused = true;
        return;
    }
    const double now = current_time();
    if(ambient_paused) { next_ambient = now + 5 + Pick(11); ambient_paused = false; }
    if(ambient_planet != planet)
    {
        Mix_HaltChannel(ambience_channel);
        ambient_planet = planet; last_ambient = -1;
        next_ambient = now + 5 + Pick(11);
    }
    if(now < next_ambient || Mix_Playing(ambience_channel)) return;
    const int count = ambient_count[planet];
    int choice = Pick(count);
    if(last_ambient >= 0 && count > 1)
    {
        choice = Pick(count-1);
        if(choice >= last_ambient) choice++;
    }
    Mix_Chunk *chunk = ambience[planet][choice].snd_chunk;
    if(chunk) ZMix_PlayChannel(ambience_channel, chunk, 0);
    last_ambient = choice;
    next_ambient = now + ChunkSeconds(chunk) + 5 + Pick(11);
}

void ZSoundEngine::BeginRenderAudio() { jack_visible = false; }
void ZSoundEngine::MarkJackhammer() { jack_visible = true; }
void ZSoundEngine::EndRenderAudio(bool paused)
{
    if(!finished_init || !battle_audio || result_audio) return;
    if(paused || !jack_visible) { if(Mix_Playing(jack_channel)) Mix_HaltChannel(jack_channel); return; }
    if(!Mix_Playing(jack_channel) && jackhammer.snd_chunk)
        ZMix_PlayChannel(jack_channel,jackhammer.snd_chunk,-1);
}
