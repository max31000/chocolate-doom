// --js-library of the Doom web build (README.Web.md).
addToLibrary({
  // SDL_SetWindowTitle would set document.title; the host page owns its title.
  emscripten_set_window_title: (title) => {},
});
