#ifndef _CONF_BLA_H
#define _CONF_BLA_H

#define BLA_CONFIG_FILE "bla.conf"

/* Forward declarations */
struct ast_channel;
struct ast_cli_entry;

/* BLA Application Strings */
extern const char bla_station_app[];
extern const char bla_trunk_app[];

/* BLA Function Prototypes */
int bla_load_config(int reload);
void bla_destroy(void);
int bla_trunk_exec(struct ast_channel *chan, const char *data);
int bla_station_exec(struct ast_channel *chan, const char *data);
char *bla_show_stations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *bla_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
enum ast_device_state bla_devstate(const char *data);
/* BLA Stasis Debugging Prototypes */
int bla_stasis_init(void);
void bla_stasis_shutdown(void);

#endif
