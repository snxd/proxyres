#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool proxy_config_env_get_auto_discover(void);
char *proxy_config_env_get_auto_config_url(void);
char *proxy_config_env_get_proxy(const char *protocol);
char *proxy_config_env_get_bypass_list(void);

bool proxy_config_env_init(void);
bool proxy_config_env_uninit(void);

proxy_config_i_s *proxy_config_env_get_interface(void);

#ifdef __cplusplus
}
#endif
