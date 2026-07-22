#ifdef _XBOX

#if defined(_GAME)
}
using namespace game;
#elif defined(_CGAME)
}
using namespace cgame;
#elif defined(_UI)
}
using namespace ui;
#else
// Engine-side code does not enter one of the VM namespaces.
#endif

#endif
