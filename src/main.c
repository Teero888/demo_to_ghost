#include <stdio.h>

#define DDNET_DEMO_IMPLEMENTATION
#include <ddnet_demo.h>
#include <ddnet_ghost/ghost.h>

#define MAX_PLAYERS 128

// Helper to convert Teeworlds' integer-based string format to a C string
bool str_utf8_check(const char *str) {
  const unsigned char *bytes = (const unsigned char *)str;
  while (*bytes) {
    if (( // ASCII
            *bytes >= 0x00 && *bytes <= 0x7F) ||
        ( // 2-byte sequence
            *bytes >= 0xC2 && *bytes <= 0xDF) &&
            (bytes[1] >= 0x80 && bytes[1] <= 0xBF)) {
      bytes += 1;
    } else if (( // 3-byte sequence
                   *bytes == 0xE0) &&
               (bytes[1] >= 0xA0 && bytes[1] <= 0xBF) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF)) {
      bytes += 2;
    } else if ((( // 3-byte sequence
                    *bytes >= 0xE1 && *bytes <= 0xEC) ||
                *bytes == 0xEE || *bytes == 0xEF) &&
               (bytes[1] >= 0x80 && bytes[1] <= 0xBF) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF)) {
      bytes += 2;
    } else if (( // 3-byte sequence
                   *bytes == 0xED) &&
               (bytes[1] >= 0x80 && bytes[1] <= 0x9F) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF)) {
      bytes += 2;
    } else if (( // 4-byte sequence
                   *bytes == 0xF0) &&
               (bytes[1] >= 0x90 && bytes[1] <= 0xBF) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF) &&
               (bytes[3] >= 0x80 && bytes[3] <= 0xBF)) {
      bytes += 3;
    } else if (( // 4-byte sequence
                   *bytes >= 0xF1 && *bytes <= 0xF3) &&
               (bytes[1] >= 0x80 && bytes[1] <= 0xBF) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF) &&
               (bytes[3] >= 0x80 && bytes[3] <= 0xBF)) {
      bytes += 3;
    } else if (( // 4-byte sequence
                   *bytes == 0xF4) &&
               (bytes[1] >= 0x80 && bytes[1] <= 0x8F) &&
               (bytes[2] >= 0x80 && bytes[2] <= 0xBF) &&
               (bytes[3] >= 0x80 && bytes[3] <= 0xBF)) {
      bytes += 3;
    } else {
      return false;
    }
  }
  return true;
}

bool ints_to_str(const int *pInts, size_t NumInts, char *pStr, size_t StrSize) {
  if (NumInts == 0 || StrSize < NumInts * sizeof(int)) {
    return false;
  }

  size_t StrIndex = 0;
  for (size_t IntIndex = 0; IntIndex < NumInts; IntIndex++) {
    const int CurrentInt = pInts[IntIndex];
    pStr[StrIndex] = ((CurrentInt >> 24) & 0xff) - 128;
    StrIndex++;
    pStr[StrIndex] = ((CurrentInt >> 16) & 0xff) - 128;
    StrIndex++;
    pStr[StrIndex] = ((CurrentInt >> 8) & 0xff) - 128;
    StrIndex++;
    pStr[StrIndex] = (CurrentInt & 0xff) - 128;
    StrIndex++;
  }
  pStr[StrIndex - 1] = '\0';

  if (str_utf8_check(pStr)) {
    return true;
  }
  pStr[0] = '\0';
  return false;
}

typedef struct {
  bool active;
  char name[16];
  ghost_t *ghost;
  int finish_time;
} player_ghost_t;

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <demo_file.demo>\n", argv[0]);
    return 1;
  }

  const char *demo_path = argv[1];
  FILE *f_demo = fopen(demo_path, "rb");
  if (!f_demo) {
    perror("Error opening demo file");
    return 1;
  }

  dd_demo_reader *dr = demo_r_create();
  if (!demo_r_open(dr, f_demo)) {
    fprintf(stderr, "Error: Could not read demo file header.\n");
    fclose(f_demo);
    demo_r_destroy(&dr);
    return 1;
  }

  const dd_demo_info *info = demo_r_get_info(dr);
  printf("Opened demo: %s\n", demo_path);
  printf("  Map: %s\n", info->header.map_name);
  printf("  Length: %ds\n", info->length);
  printf("  Timestamp: %s\n", info->header.timestamp);

  player_ghost_t players[MAX_PLAYERS];
  for (int i = 0; i < MAX_PLAYERS; i++) {
    players[i].active = false;
    players[i].ghost = NULL;
    players[i].finish_time = -1;
  }

  dd_demo_chunk chunk;
  uint8_t snap_buf[DD_MAX_SNAPSHOT_SIZE];

  printf("Processing demo...\n");

  while (demo_r_next_chunk(dr, &chunk)) {
    if (chunk.type != DD_CHUNK_SNAP && chunk.type != DD_CHUNK_SNAP_DELTA) {
      continue;
    }

    const dd_snapshot *snap;
    if (chunk.type == DD_CHUNK_SNAP_DELTA) {
      int size = demo_r_unpack_delta(dr, chunk.data, chunk.size, snap_buf);
      if (size < 0) {
        fprintf(stderr, "Error unpacking delta snapshot.\n");
        continue;
      }
      snap = (const dd_snapshot *)snap_buf;
    } else {
      snap = (const dd_snapshot *)chunk.data;
    }

    for (int i = 0; i < snap->num_items; i++) {
      const dd_snap_item *item = dd_snap_get_item(snap, i);
      int type = dd_snap_item_type(item);
      int id = dd_snap_item_id(item);

      if (id >= MAX_PLAYERS)
        continue;

      if (type == DD_NETOBJTYPE_CLIENTINFO) {
        const dd_netobj_client_info *cinfo =
            (const dd_netobj_client_info *)dd_snap_item_data(item);
        if (!players[id].active) {
          players[id].active = true;
          players[id].ghost = ghost_create();
          ints_to_str(cinfo->m_aName, 4, players[id].name,
                      sizeof(players[id].name));
          printf("Found player: %s (ID: %d)\n", players[id].name, id);

          char skin_name[24];
          ints_to_str(cinfo->m_aSkin, 6, skin_name, sizeof(skin_name));
          ghost_set_skin(players[id].ghost, skin_name, cinfo->m_UseCustomColor,
                         cinfo->m_ColorBody, cinfo->m_ColorFeet);
        }
      } else if (type == DD_NETOBJTYPE_CHARACTER) {
        if (players[id].active) {
          const dd_netobj_character *demo_char =
              (const dd_netobj_character *)dd_snap_item_data(item);
          ghost_character_t ghost_char;
          ghost_char.tick = demo_char->core.m_Tick;
          ghost_char.x = demo_char->core.m_X;
          ghost_char.y = demo_char->core.m_Y;
          ghost_char.vel_x = demo_char->core.m_VelX;
          ghost_char.vel_y = demo_char->core.m_VelY;
          ghost_char.angle = demo_char->core.m_Angle;
          ghost_char.direction = demo_char->core.m_Direction;
          ghost_char.weapon = demo_char->m_Weapon;
          ghost_char.hook_state = demo_char->core.m_HookState;
          ghost_char.hook_x = demo_char->core.m_HookX;
          ghost_char.hook_y = demo_char->core.m_HookY;
          ghost_char.attack_tick = demo_char->m_AttackTick;
          ghost_add_snap(players[id].ghost, &ghost_char);
        }
      } else if (type == DD_NETMSGTYPE_SV_DDRACETIME) {
        // For now, we'll just use the demo length as the finish time.
      }
    }
  }

  printf("Finished processing. Saving ghost files...\n");

  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (players[i].active && players[i].ghost->path.num_items > 0) {
      // Use demo length for now
      int finish_time_ms = info->length * 1000;
      ghost_set_meta(players[i].ghost, players[i].name, info->header.map_name,
                     finish_time_ms);

      char crc_str[65];
      if (info->has_sha256) {
        for (int j = 0; j < 32; j++)
          sprintf(&crc_str[j * 2], "%02x", info->map_sha256[j]);
      } else {
        sprintf(crc_str, "%08x", info->map_crc);
      }

      char filename[256];
      snprintf(filename, sizeof(filename), "%s_%s_%.3f_%s_%s.gho",
               info->header.map_name, players[i].name, finish_time_ms / 1000.0f,
               info->header.timestamp, crc_str);

      // Replace spaces in timestamp with dashes
      for (char *p = filename; *p; ++p) {
        if (*p == ' ') {
          *p = '-';
        }
      }

      if (ghost_save(players[i].ghost, filename) == 0) {
        printf("Saved ghost for %s to %s\n", players[i].name, filename);
      } else {
        fprintf(stderr, "Error saving ghost for %s\n", players[i].name);
      }
    }
  }

  // Cleanup
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (players[i].ghost) {
      ghost_free(players[i].ghost);
    }
  }
  demo_r_destroy(&dr);

  return 0;
}
