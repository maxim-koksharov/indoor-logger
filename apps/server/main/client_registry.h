#ifndef CLIENT_REGISTRY_H
#define CLIENT_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLIENT_REGISTRY_MAX_CLIENTS 16
#define CLIENT_REGISTRY_ID_LEN 32
#define CLIENT_REGISTRY_NAME_LEN 32
#define CLIENT_REGISTRY_IP_LEN 16

/** @brief Information about a registered client device. */
typedef struct {
    char     id[CLIENT_REGISTRY_ID_LEN];       /**< Unique client identifier */
    char     name[CLIENT_REGISTRY_NAME_LEN];   /**< Human-readable name */
    uint32_t last_seen;                        /**< Timestamp of last upload */
    bool     online;                           /**< Whether client is currently online */
    char     ip_str[CLIENT_REGISTRY_IP_LEN];   /**< Last known IP address */
} client_info_t;

/** @brief Pending client ID rename mapping. Used to route uploads from a client
 *  that still uses the old ID after the server has been reconfigured. */
typedef struct {
    char old_id[CLIENT_REGISTRY_ID_LEN];
    char new_id[CLIENT_REGISTRY_ID_LEN];
} id_change_mapping_t;

/** @brief Initialize the client registry (clears all entries).
 *  @return 0 on success.
 */
int client_registry_init(void);

/** @brief Register or update a client's presence.
 *  @param id   Client identifier (required).
 *  @param name Optional display name (can be NULL or empty).
 *  @param ip   Optional IP string (can be NULL or empty).
 *  @return 0 on success, -1 if the registry is full.
 */
int client_registry_update(const char *id, const char *name, const char *ip);

/** @brief Look up a client by ID.
 *  @param id Client identifier.
 *  @return Pointer to client_info_t, or NULL if not found.
 */
client_info_t *client_registry_get(const char *id);

/** @brief Resolve a client ID through the pending-rename mapping table.
 *  If @p id matches an old ID in the mapping, returns the new ID; otherwise
 *  returns @p id unchanged. The returned pointer is valid for the lifetime of
 *  the registry and must not be freed.
 *  @param id Client identifier (old or current).
 *  @return Resolved ID (new ID if a mapping exists, otherwise the input).
 */
const char *client_registry_resolve_id(const char *id);

/** @brief Rename a client's ID in place, preserving name/IP/status/basic_mode.
 *  Adds a pending mapping so that uploads still using the old ID are routed
 *  to the renamed entry.
 *  @param old_id Existing client identifier.
 *  @param new_id New client identifier (must not already exist).
 *  @return 0 on success, -1 if old_id is unknown, new_id is already used,
 *          or the registry is full.
 */
int client_registry_rename_id(const char *old_id, const char *new_id);

/** @brief Clear a pending ID rename mapping (called when the client confirms
 *  the new ID by uploading under it).
 *  @param old_id The old ID of the mapping to remove.
 */
void client_registry_clear_id_mapping(const char *old_id);

/** @brief Get all registered clients.
 *  @param out      Output buffer for client_info_t array.
 *  @param capacity Buffer capacity (number of entries).
 *  @return Number of clients copied into @p out.
 */
int client_registry_get_all(client_info_t *out, int capacity);

/** @brief Mark clients as offline if not seen within the timeout.
 *  @param timeout_sec  Maximum seconds since last_seen before marking stale.
 */
void client_registry_check_stale(uint32_t timeout_sec);

/** @brief Get the basic-mode flag for a client.
 *  @param id Client identifier.
 *  @return true if the client is in basic mode (temp+humidity only), false otherwise.
 *          Returns true by default for unknown clients.
 */
bool client_registry_get_basic_mode(const char *id);

/** @brief Set the basic-mode flag for a client.
 *  @param id    Client identifier.
 *  @param value true to enable basic mode, false to disable.
 *  @return 0 on success, -1 if the client is not found.
 */
int client_registry_set_basic_mode(const char *id, bool value);

/** @brief Save the registry to NVS for persistence across reboots.
 *  @return 0 on success, -1 on NVS error.
 */
int client_registry_save(void);

/** @brief Load the registry from NVS.
 *  @return Number of clients loaded, or 0 if none found.
 */
int client_registry_load(void);

/** @brief Delete a client from the registry.
 *  @param id Client identifier.
 *  @return 0 on success, -1 if not found.
 */
int client_registry_delete(const char *id);

#ifdef __cplusplus
}
#endif

#endif
