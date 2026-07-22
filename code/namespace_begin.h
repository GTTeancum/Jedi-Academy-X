#ifdef _XBOX

#if defined(_GAME)
namespace game
{
#elif defined(_CGAME)
namespace cgame
{
#elif defined(_UI)
namespace ui
{
#else
// Engine-side code does not enter one of the VM namespaces.
#endif

#endif
