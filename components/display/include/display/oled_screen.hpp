#pragma once

class Oled {
   public:
    void BootDisplay();
    void WatchDisplay();
    void RecvNotif();
    void ShowImage(const unsigned char img[]);
};
