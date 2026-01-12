#pragma once

/**
 * @brief Task kontrolera automatycznego podlewania
 * 
 * Task sprawdza wilgotność gleby i gdy jest poniżej progu,
 * jedzie do przodu aż do wykrycia przeszkody (ściany).
 * 
 * @param param Nieużywany (może być NULL)
 */
void water_controller_task(void *param);

