#pragma once

class Prisma {
    static inline bool createdView = false;
public:
    static void Install();
    static void Show();
    static void Hide();
    static bool IsHidden();
};