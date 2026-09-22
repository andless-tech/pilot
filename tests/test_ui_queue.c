#include "../src/ui.c"
#include <assert.h>

int main(void) {
    pilot_ui *u=calloc(1,sizeof(*u)); assert(u);
    pilot_ui_control controls[32];
    for(unsigned i=0;i<32;i++) controls[i]=(pilot_ui_control){.id=i+1,.kind=PILOT_UI_TEXT,.label="Text",.maximum=100};
    pilot_ui_page_options options={.title="Limits",.controls=controls,.count=32};
    pilot_ui_page id; assert(pilot_ui_register(u,&options,&id)==PILOT_OK);
    ui_page *p=page(u,id);
    /* A lost registration reply keeps the frozen baseline, not a fresh page. */
    p->registration_sent=1; memcpy(p->confirmed,p->current,sizeof(p->confirmed));
    char text[128]; memset(text,'x',127); text[127]=0;
    assert(pilot_ui_begin(u,id)==PILOT_OK);
    for(unsigned i=1;i<=32;i++) assert(pilot_ui_set_text(u,id,i,text)==PILOT_OK);
    assert(pilot_ui_commit(u,id)==PILOT_UI_BUSY); assert(pilot_ui_cancel(u,id)==PILOT_OK);
    for(unsigned i=1;i<=29;i++) assert(pilot_ui_set_text(u,id,i,text)==PILOT_OK);
    assert(pilot_ui_set_text(u,id,30,text)==PILOT_UI_BUSY);
    assert(!p->current[29].text[0]);
    for(unsigned i=1;i<=16;i++) assert(pilot_ui_complete(u,id,i,true)==PILOT_OK);
    assert(pilot_ui_complete(u,id,17,true)==PILOT_UI_BUSY && u->count==16);
    printf("PASS SDK bounded queue and lost-registration baseline; client=%zu bytes\n",sizeof(*u));
    pilot_ui_close(u);
}
