// Copyright (C) 2017 Pierre Parent <pierre.parent ''at=- pparent.fr>
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <config.h>
#include <menu.h>
#include <math.h>
#include <lens.h>
#include <imath.h>

static int video_av_running;



// configs
static CONFIG_INT("movie.av_expo.enabled", av_expo_enabled, 0);
static CONFIG_INT("movie.av_expo.av_value", av_value, 30);
static CONFIG_INT("movie.av_expo.smooth_changes", smooth_changes, 1);

extern void set_movie_digital_iso_gain_for_gradual_expo(int gain);
extern int prop_set_rawiso_approx(unsigned iso);

void FAST digital_gain_simulate_virtual_iso (int current_virtual_iso,int current_frame_iso)
{
            if (smooth_changes)
            {
            set_movie_digital_iso_gain_for_gradual_expo(1024.0f * powf(2, -1.0f*(current_frame_iso-current_virtual_iso)/8.0f));
            }
            else
              set_movie_digital_iso_gain_for_gradual_expo(1024.0f);  
}

int tv_step(int *current_tv,int desired_tv,int *shutter_iso_compensation, int * current_virtual_iso)
{
         
        if ((*current_tv)<0x60 || (*current_tv)>0x98)
             return 0;
             
        int previous_tv=(*current_tv);
         int diff=(*current_tv)-(int)COERCE(desired_tv,0x60, 0x98);
         
         //we don't need to change tv
         if (diff==0)
         {
                return 0;
         }
         
         int change_made=0;
         int h;
         //If possible jump of 8: it is faster and smoother because real ISO compensation cannot
         //take every value but can take every value % 4;

         int dontcompensate=0;
                
         //If possible jump of as many 4: it is faster and smoother because real ISO compensation cannot
         //take every value but can take every value % 4;
         for (h=32;h>=4;h-=4)
         {
            if (!change_made)
                {
                if(diff<-(h-1) && MAX_ISO-(*current_virtual_iso)>h)
                        {(*current_tv)=(*current_tv)+h;change_made=1;}
                if(diff>(h-1)  &&  (*current_virtual_iso)-MIN_ISO>h )
                        {(*current_tv)=(*current_tv)-h;change_made=1;}
                }
         }
         
         //If we could not achieve a 4 step then do a 1 step
//          if (!change_made)
//          {
//          if(diff<0 && ABS((*shutter_iso_compensation))<4 && MAX_ISO-(*current_virtual_iso)>0 )
//             {(*current_tv)=(*current_tv)+1;dontcompensate=1;}
//          if(diff>0  && ABS((*shutter_iso_compensation))<4 && (*current_virtual_iso)-MIN_ISO>0 )
//             {(*current_tv)=(*current_tv)-1;;dontcompensate=1;}
//          }
//        
         //Ensure that we have an acceptable value
         (*current_tv)=COERCE((*current_tv), 0x60, 0x98); 
         
         if ((*current_virtual_iso)+(*current_tv)-previous_tv>=MIN_ISO &&
             (*current_virtual_iso)+(*current_tv)-previous_tv<=MAX_ISO)
         {
        //set rawshutter and shutter iso compensation
        lens_info.raw_shutter=(*current_tv);
         (*current_virtual_iso)+=(*current_tv)-previous_tv;
            
         set_movie_digital_iso_gain_for_gradual_expo(1024.0f);
            bv_set_rawiso(*current_virtual_iso);
            bv_set_rawshutter((*current_tv)); 
            
            if ((*current_tv)-previous_tv!=0)
                while(read_frame_iso()!=(*current_virtual_iso));
                  msleep(10);
           msleep(50);
                shutter_iso_compensation=0;
//               bmp_printf(FONT_MED,100,150,"test1  %d %d",(*current_tv)-previous_tv,(*shutter_iso_compensation));
            return 1;
         }
         
         return 0;
        
         
}

void iso_step(int *current_virtual_iso,int current_frame_iso,int *last_requested_iso,int desired_iso,int * shutter_iso_compensation, int *virtual_iso_at_last_request)
{
        unsigned analog_iso,digital_gain,value_to_set;
        static int wait_for_set=0;
        
        if ((*last_requested_iso)==current_frame_iso)
            wait_for_set=0;
        
        int previous_virtual_iso=(*current_virtual_iso);
        
        if (desired_iso !=0)
        {
            //Increase or decrease virtual iso in the desired direction
            if((*current_virtual_iso)-desired_iso <0 )
                        (*current_virtual_iso)++;

            if((*current_virtual_iso)-desired_iso >0 )
                        (*current_virtual_iso)--;
        }
        
         
        //Ensure that frame iso is not at more than 8 distance of virtual iso
          while ( (*current_virtual_iso)-current_frame_iso > 8)
              (*current_virtual_iso)--;
          while ( (*current_virtual_iso)-current_frame_iso <-8)
              (*current_virtual_iso)++;      
          
          //The value we should try to set physically to the sensor
          //Is our virtual iso, plus compensation for shutter.
            value_to_set=(*current_virtual_iso);
          
          value_to_set=COERCE(value_to_set,MIN_ISO,MAX_ISO);
          
          //If the value to set is a full analog ISO, then request to set it
          split_iso(value_to_set, &analog_iso, &digital_gain);
          if (digital_gain==0 || smooth_changes==0 ||  (*shutter_iso_compensation)!=0  )
          {
              if (!wait_for_set||!smooth_changes)
              {
                bv_set_rawiso(value_to_set);
//                 bmp_printf(FONT_MED,200,350,"test2  %d %d %d",(*current_virtual_iso),*shutter_iso_compensation,value_to_set);
                (*last_requested_iso)=value_to_set;
                (*virtual_iso_at_last_request)=(*current_virtual_iso);
                wait_for_set=1;
              }
          }
}


#define AE_SPEED 60

static void FAST video_av_task()
{
    int current_virtual_iso,current_frame_iso,last_requested_iso,desired_iso,virtual_iso_at_last_request;
    int current_tv, desired_tv, shutter_iso_compensation=0;
    video_av_running = 1;
    
    int t0 = get_ms_clock_value();
    int t1=t0;
    int t2=get_ms_clock_value(),t3=t2;
    //Init values       
    desired_iso=lens_info.raw_iso_ae; 
    current_frame_iso=read_frame_iso();last_requested_iso=0;current_virtual_iso=lens_info.raw_iso;
    desired_tv=lens_info.raw_shutter_ae;current_tv=desired_tv;
    virtual_iso_at_last_request=lens_info.raw_iso;
    unsigned int kk=0;
    int last_frame_iso;
    
    TASK_LOOP
    {
        if (  !gui_menu_shown() && av_expo_enabled && expo_override_active() &&  shooting_mode == SHOOTMODE_MOVIE )
        {
            
        t0 = get_ms_clock_value();
        
        current_frame_iso=read_frame_iso();
        if (t0-t1>=AE_SPEED)
        {
      
            //Update desired values given by canon AE
            if (desired_iso==0)
            {
            desired_iso=(int)(lens_info.raw_iso_ae);
            desired_tv=lens_info.raw_shutter_ae;
            }
            kk++;
            bmp_printf(FONT_MED,300,300,"TV XXXXXXXXXXXXX");
             bmp_printf(FONT_MED,300,300,"TV %d %d %d",current_tv,desired_tv,shutter_iso_compensation);
             
            bmp_printf(FONT_MED,50,50,"ISO XXXXXXXXXXXXX");
             bmp_printf(FONT_MED,50,50,"ISO %d %d %d %d",current_frame_iso,current_virtual_iso,last_requested_iso,desired_iso);
             
            if( current_frame_iso == last_requested_iso && shutter_iso_compensation==0 && current_virtual_iso==current_frame_iso && current_virtual_iso==desired_iso)
            {
                tv_step(&current_tv,desired_tv,&shutter_iso_compensation,&current_virtual_iso);
                current_frame_iso=read_frame_iso();
                last_requested_iso=current_frame_iso;
            }
            else
            iso_step(&current_virtual_iso,current_frame_iso,&last_requested_iso,desired_iso,
                     &shutter_iso_compensation,&virtual_iso_at_last_request);
 
            t1=t0;
            
        }
                
        
        
        //If last requested values have been applied to sensor
        if( current_frame_iso == last_requested_iso || current_frame_iso == 0 || smooth_changes==0 )
            {
            //Reset shutter compensation when values where applied;
            current_virtual_iso+=shutter_iso_compensation;
//             if(shutter_iso_compensation!=0)
//                 bmp_printf(FONT_MED,0,0,"test3  %d %d %d",current_frame_iso,last_requested_iso,current_virtual_iso );
            digital_gain_simulate_virtual_iso(current_virtual_iso,current_frame_iso);
            shutter_iso_compensation=0;

            last_frame_iso=current_frame_iso;
      
            //Update desired values given by canon AE
            desired_iso=(int)(lens_info.raw_iso_ae);
            desired_tv=lens_info.raw_shutter_ae;
            
            //Update Av value if needed
            av_value=COERCE(av_value,lens_info.raw_aperture_min,lens_info.raw_aperture_max);
            if(av_value!=lens_info.raw_aperture)
                lens_set_rawaperture(av_value);
            
            
            if (current_virtual_iso==0)
                current_virtual_iso=(int)(lens_info.raw_iso);
            
            //We can rest and wait because there won't be any value change
            //In FRAME_ISO which could provoke glitch.
            msleep(AE_SPEED);
            }
            else 
            { 
                digital_gain_simulate_virtual_iso(current_virtual_iso,current_frame_iso); 
            }
        
        }
        else
        {
            msleep(5000);
            current_virtual_iso=lens_info.raw_iso;
            last_requested_iso=lens_info.raw_iso;
            current_frame_iso=lens_info.raw_iso;
            current_tv=lens_info.raw_shutter;
            shutter_iso_compensation=0;
            virtual_iso_at_last_request=lens_info.raw_iso;
            digital_gain_simulate_virtual_iso(current_virtual_iso,current_frame_iso);
        }
    }
    
    goto quit;
quit:
    video_av_running = 0;
}



static unsigned int video_av_shoot_task(){
    if(av_expo_enabled &&
        shooting_mode == SHOOTMODE_MOVIE &&
        !video_av_running
    )
        task_create("video_av", 0x1c, 0x1000, video_av_task, (void*)0);

    return 0;
}


static MENU_UPDATE_FUNC(menu_custom_display_av) {
    int av_disp=values_aperture[raw2index_aperture(av_value)];
    MENU_SET_VALUE("%d.%d", av_disp/10,av_disp%10);
}


static struct menu_entry video_av_menu[] =
{
    {
         .name = "Av exposure",
         .priv = &av_expo_enabled,
         .max = 1,
        .help = "Av semi-automatic exposure for video.",
        .help2 = "Exposure overide must be set.",
        .children = (struct menu_entry[])
        {
            {
                .name = "Av",
                .priv = &av_value,
                .update = menu_custom_display_av,
                .max = 32,
                .min = 22,
            },
            {
                .name = "Smooth changes",
                .priv = &smooth_changes,
                .max = 1
            },
            MENU_EOL,
        }
    }
};

static unsigned int video_av_init() {
    video_av_menu->children[0].max=lens_info.raw_aperture_max;
    video_av_menu->children[0].min=lens_info.raw_aperture_min;
    av_value=COERCE(av_value,lens_info.raw_aperture_min,lens_info.raw_aperture_max);
    menu_add("Movie", video_av_menu, COUNT(video_av_menu));
    return 0;
}

static unsigned int video_av_deinit() {
        return 0;
}

MODULE_INFO_START()
    MODULE_INIT(video_av_init)
    MODULE_DEINIT(video_av_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_SHOOT_TASK, video_av_shoot_task, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
      MODULE_CONFIG(av_expo_enabled)
      MODULE_CONFIG(av_value)
      MODULE_CONFIG(smooth_changes)      
MODULE_CONFIGS_END()
