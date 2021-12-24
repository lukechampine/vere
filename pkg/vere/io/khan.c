/* vere/khan.c
**
**  implements the control plane: a socket that can be used to
**  query and interact with an urbit ship from earth. supports
**  three basic request types:
**
**  - %fyrd: a request to run a thread; will be forwarded to arvo
**           to be dispatched by the khan vane.
**
**  - %peek: namespace peek request. mostly forwarded to arvo,
**           except for a namespace overlay for runtime
**           information.
**
**  - %move: kernel event. injected, with a runtime overlay.
**
**  messages use newt framing. because the framing begins with
**  a magic byte (^I, horizontal tab), any messages that do not
**  contain that byte are reserved for future use.
**
*/
#include <inttypes.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <uv.h>
#include <errno.h>

#include "all.h"
#include "vere/vere.h"

#ifdef  _WIN32

u3_auto*
u3_khan_io_init(u3_pier* pir_u)
{
  return NULL;
}

#else   //  _WIN32

/* u3_cran: control plane request.
*/
  typedef struct _u3_cran {
    c3_l              rid_l;            //  client-supplied request id
    struct _u3_chan*  can_u;            //  connection backpointer
    struct _u3_cran*  nex_u;            //  next pointer
  } u3_cran;

/* u3_chan: incoming control plane connection.
*/
  typedef struct _u3_chan {
    struct _u3_moor   mor_u;            //  message handler
    c3_l              coq_l;            //  connection number
    c3_w              red_w;            //  retry counter
    struct _u3_shan*  san_u;            //  server backpointer
    struct _u3_cran*  ran_u;            //  request list
  } u3_chan;

/* u3_shan: control plane server.
*/
  typedef struct _u3_shan {
    uv_pipe_t         pyp_u;            //  server stream handler
    c3_l              nex_l;            //  next connection number
    struct _u3_khan*  kan_u;            //  device backpointer
    struct _u3_chan*  can_u;            //  connection list
  } u3_shan;

/* u3_khan: control plane device.
*/
  typedef struct _u3_khan {
    u3_auto           car_u;            //  driver
    c3_l              sev_l;            //  instance number
    struct _u3_shan*  san_u;            //  server reference
    u3_cue_xeno*      sil_u;            //  cue handle
  } u3_khan;

static const c3_c URB_SOCK_PATH[] = ".urb/khan.sock";

/* _khan_close_cb(): socket close callback.
*/
static void
_khan_close_cb(uv_handle_t* had_u)
{
  c3_free(had_u);
}

/* _khan_mote_free(): u3_moat-shaped close callback.
*/
static void
_khan_moat_free(void* ptr_v, ssize_t err_i, const c3_c* err_c)
{
  c3_free(ptr_v);
}

/* _khan_punt_goof(): print stack trace of error.
*/
static void
_khan_punt_goof(u3_noun lud)
{
  if ( 2 == u3qb_lent(lud) ) {
    u3_pier_punt_goof("khan", u3k(u3h(lud)));
    u3_pier_punt_goof("crud", u3k(u3h(u3t(lud))));
  }
  else {
    u3_noun dul = lud;
    c3_w    len_w = 1;

    while ( u3_nul != dul ) {
      u3l_log("khan: bail %u\r\n", len_w++);
      u3_pier_punt_goof("khan", u3k(u3h(dul)));
      dul = u3t(dul);
    }
  }

  u3z(lud);
}

/* _khan_send_noun(): jam and send noun over chan.
*/
static void
_khan_send_noun(u3_chan* can_u, u3_noun nun)
{
  c3_y* byt_y;
  c3_d  len_d;

  u3s_jam_xeno(nun, &len_d, &byt_y);
  u3z(nun);
  u3_newt_send((u3_mojo*)&can_u->mor_u, len_d, byt_y);
}

/* _khan_poke_bail(): error function on failed %fyrd.
*/
static void
_khan_poke_bail(u3_ovum* egg_u, u3_noun lud)
{
  u3_khan* kan_u = (u3_khan*)egg_u->car_u;

  // TODO: find request and produce error packet
  _khan_punt_goof(lud);
  u3_ovum_free(egg_u);
}

/* _khan_close_chan(): send a close event to arvo and stop reading.
*/
static void
_khan_close_socket(u3_khan* kan_u, u3_chan* can_u)
{
  u3_noun wir, cad;

  wir = u3nq(c3__khan,
             u3dc("scot", c3__uv, kan_u->sev_l),
             u3dc("scot", c3__ud, can_u->coq_l),
             u3_nul);
  cad = u3nc(c3__done, u3_nul);
  u3_auto_peer(
    u3_auto_plan(&kan_u->car_u,
                 u3_ovum_init(0, c3__k, wir, cad)),
    0, 0, _khan_poke_bail);
  u3_newt_moat_stop((u3_moat*)&can_u->mor_u, _khan_moat_free);
}

/* _khan_close_chan(): close given channel, freeing.
*/
static void
_khan_close_chan(u3_chan* can_u, u3_shan* san_u)
{
  u3_chan* inn_u;
  u3_cran* ran_u;

  //  unset chan on all pending requests.
  //
  for ( ran_u = can_u->ran_u; ran_u; ran_u = ran_u->nex_u ) {
    ran_u->can_u = 0;
  }

  //  remove chan from server's connection list.
  //
  if ( san_u->can_u == can_u ) {
    san_u->can_u = (u3_chan*)can_u->mor_u.nex_u;
  }
  else {
    for ( inn_u = san_u->can_u; inn_u; inn_u = (u3_chan*)inn_u->mor_u.nex_u ) {
      if ( (u3_chan*)inn_u->mor_u.nex_u == can_u ) {
        inn_u->mor_u.nex_u = can_u->mor_u.nex_u;
        break;
      }
    }
  }
  can_u->mor_u.nex_u = NULL;
  _khan_close_socket(san_u->kan_u, can_u);
}

/* _khan_moor_bail(): error callback for u3_moor.
*/
static void
_khan_moor_bail(void* ptr_v, ssize_t err_i, const c3_c* err_c)
{
  u3_chan*  can_u = (u3_chan*)ptr_v;
  u3_shan*  san_u = can_u->san_u;

  if ( err_i == UV_EOF ) {
    _khan_close_chan(can_u, san_u);
  }
  else {
    u3_noun bal;

    if ( 3 >= can_u->red_w ) {
      u3l_log("khan: moor fatal %zd %s\n", err_i, err_c);
      _khan_close_chan(can_u, san_u);
    }
    else {
      u3l_log("khan: moor bail %zd %s\n", err_i, err_c);
      can_u->red_w++;
      //  TODO: rethink.
      //
      bal = u3nq(c3__bail,
                 u3i_string("driver"),
                 -err_i & 0x7fffffff,
                 u3i_string(err_c));
      _khan_send_noun(can_u, bal);
    }
  }
}

/* _khan_peek_cb(): scry result handler.
*/
static void
_khan_peek_cb(void* ptr_v, u3_noun res)
{
  u3_cran* ran_u = (u3_cran*)ptr_v;
  u3_chan* can_u = ran_u->can_u;
  u3_cran* inn_u;

  if ( !can_u ) {
    //  chan was closed; noop.
    //
    c3_free(ran_u);
    u3z(res);
    return;
  }
  _khan_send_noun(can_u, u3nt(c3__peek, ran_u->rid_l, res));

  //  remove this request from the pending list.
  //
  if ( ran_u == can_u->ran_u ) {
    can_u->ran_u = ran_u->nex_u;
  }
  else {
    for ( inn_u = can_u->ran_u; inn_u; inn_u = inn_u->nex_u ) {
      if ( ran_u == inn_u->nex_u ) {
        inn_u->nex_u = ran_u->nex_u;
        break;
      }
    }
  }
  c3_free(ran_u);
}

/* _khan_moor_poke(): called on message read from u3_moor.
*/
static void
_khan_moor_poke(void* ptr_v, c3_d len_d, c3_y* byt_y)
{
  u3_weak   jar;
  u3_noun   can, rid, tag, dat;
  u3_chan*  can_u = (u3_chan*)ptr_v;
  u3_khan*  kan_u = can_u->san_u->kan_u;

  jar = u3s_cue_xeno_with(kan_u->sil_u, len_d, byt_y);
  if ( u3_none == jar ) {
    can_u->mor_u.bal_f(can_u, -1, "cue-none");
    return;
  }
  if ( (c3n == u3r_cell(jar, &rid, &can)) ||
       (c3n == u3r_cell(can, &tag, &dat)) ||
       (c3n == u3a_is_cat(rid)) )
  {
    can_u->mor_u.bal_f(can_u, -2, "jar-bad");
  }
  else {
    switch (tag) {
      default: {
        can_u->mor_u.bal_f(can_u, -3, "i.jar-unknown");
        break;
      }

      case c3__fyrd: {
        u3_noun wir = u3nc(c3__khan,
                           u3nq(u3dc("scot", c3__uv, kan_u->sev_l),
                                u3dc("scot", c3__ud, can_u->coq_l),
                                u3dc("scot", c3__ud, rid),
                                u3_nul));

        u3l_log("khan: fyrd %" PRIu32 "\n", rid);
        u3_auto_peer(
          u3_auto_plan(&kan_u->car_u,
                       u3_ovum_init(0, c3__k, wir, u3k(can))),
          0, 0, _khan_poke_bail);
        break;
      }

      case c3__peek: {
        u3_cran*  ran_u = c3_calloc(sizeof(u3_cran));
        u3_noun   gan = u3nc(u3_nul, u3_nul);   //  [~ ~]: read from self

        //  TODO: overlay runtime namespace.
        //
        u3l_log("khan: peek %" PRIu32 "\n", rid);
        ran_u->can_u = can_u;
        ran_u->nex_u = can_u->ran_u;
        can_u->ran_u = ran_u;
        ran_u->rid_l = (c3_l)rid;
        u3_pier_peek(kan_u->car_u.pir_u, gan, u3k(dat), ran_u, _khan_peek_cb);
        break;
      }

      case c3__move: {
        //  TODO: implement
        //
        u3l_log("khan: move %" PRIu32 "\n", rid);
        break;
      }
    }
  }
  u3z(jar);
}

/* _khan_conn_cb(): socket connection callback.
*/
static void
_khan_conn_cb(uv_stream_t* sem_u, c3_i tas_i)
{
  u3_shan*  san_u = (u3_shan*)sem_u;
  u3_khan*  kan_u = san_u->kan_u;
  u3_chan*  can_u;
  c3_i      err_i;

  can_u = c3_calloc(sizeof(u3_chan));
  can_u->mor_u.ptr_v = can_u;
  can_u->mor_u.pok_f = _khan_moor_poke;
  can_u->mor_u.bal_f = _khan_moor_bail;
  can_u->coq_l = san_u->nex_l++;
  can_u->san_u = san_u;
  err_i = uv_timer_init(u3L, &can_u->mor_u.tim_u);
  c3_assert(!err_i);
  err_i = uv_pipe_init(u3L, &can_u->mor_u.pyp_u, 0);
  c3_assert(!err_i);
  err_i = uv_accept(sem_u, (uv_stream_t*)&can_u->mor_u.pyp_u);
  c3_assert(!err_i);
  u3_newt_read((u3_moat*)&can_u->mor_u);
  can_u->mor_u.nex_u = (u3_moor*)san_u->can_u;
  san_u->can_u = can_u;
}

/* _khan_sock_init(): initialize socket device.
*/
static void
_khan_sock_init(u3_shan* san_u)
{
  // The full socket path is limited to about 108 characters, and we want it to
  // be relative to the pier. So we save our current path, chdir to the pier,
  // open the socket at the desired path, then chdir back. Hopefully there
  // aren't any threads.
  c3_c pax_c[2048];
  c3_i err_i;

  if ( NULL == getcwd(pax_c, sizeof(pax_c)) ) {
    u3l_log("khan: getcwd: %s\n", uv_strerror(errno));
    u3_king_bail();
  }
  if ( 0 != chdir(u3_Host.dir_c) ) {
    u3l_log("khan: chdir: %s\n", uv_strerror(errno));
    u3_king_bail();
  }
  if ( 0 != unlink(URB_SOCK_PATH) && errno != ENOENT ) {
    u3l_log("khan: unlink: %s\n", uv_strerror(errno));
    goto _khan_sock_err_chdir;
  }
  if ( 0 != (err_i = uv_pipe_init(u3L, &san_u->pyp_u, 0)) ) {
    u3l_log("khan: uv_pipe_init: %s\n", uv_strerror(err_i));
    goto _khan_sock_err_chdir;
  }
  if ( 0 != (err_i = uv_pipe_bind(&san_u->pyp_u, URB_SOCK_PATH)) ) {
    u3l_log("khan: uv_pipe_bind: %s\n", uv_strerror(err_i));
    goto _khan_sock_err_chdir;
  }
  if ( 0 != (err_i = uv_listen((uv_stream_t*)&san_u->pyp_u, 0,
                               _khan_conn_cb)) ) {
    u3l_log("khan: uv_listen: %s\n", uv_strerror(err_i));
    goto _khan_sock_err_unlink;
  }
  if ( 0 != chdir(pax_c) ) {
    u3l_log("khan: chdir: %s\n", uv_strerror(errno));
    goto _khan_sock_err_close;
  }
  return;

_khan_sock_err_close:
  uv_close((uv_handle_t*)&san_u->pyp_u, _khan_close_cb);
_khan_sock_err_unlink:
  if ( 0 != unlink(URB_SOCK_PATH) ) {
    u3l_log("khan: unlink: %s\n", uv_strerror(errno));
  }
_khan_sock_err_chdir:
  if ( 0 != chdir(pax_c) ) {
    u3l_log("khan: chdir: %s\n", uv_strerror(errno));
  }
  u3_king_bail();
}

/* _khan_born_news(): initialization complete, open socket.
*/
static void
_khan_born_news(u3_ovum* egg_u, u3_ovum_news new_e)
{
  u3_auto* car_u = egg_u->car_u;
  u3_khan* kan_u = (u3_khan*)car_u;
  u3_shan* san_u;

  if ( u3_ovum_done == new_e ) {
    c3_assert(!kan_u->san_u);
    san_u = c3_calloc(sizeof(*san_u));
    san_u->nex_l = 1;
    san_u->kan_u = kan_u;
    kan_u->san_u = san_u;
    _khan_sock_init(san_u);
    car_u->liv_o = c3y;
    u3l_log("khan: live on %s/%s\n", u3_Host.dir_c, URB_SOCK_PATH);
  }
}

/* _khan_born_bail(): nonessential failure; log it and keep going.
*/
static void
_khan_born_bail(u3_ovum* egg_u, u3_noun lud)
{
  u3l_log("khan: %%born failure; socket not opened\n");
  //  XX: still say we're "live" so event processing can start.
  //
  egg_u->car_u->liv_o = c3y;
}

/* _khan_io_talk(): notify %khan that we're live
*/
static void
_khan_io_talk(u3_auto* car_u)
{
  u3_khan* kan_u = (u3_khan*)car_u;
  u3_noun  wir = u3nt(c3__khan,
                      u3dc("scot", c3__uv, kan_u->sev_l),
                      u3_nul);
  u3_noun  cad = u3nc(c3__born, u3_nul);

  u3_auto_peer(
    u3_auto_plan(car_u, u3_ovum_init(0, c3__k, wir, cad)),
    0,
    _khan_born_news,
    _khan_born_bail);
}

/* _khan_search_chan(): lookup channel by connection number.
*/
static u3_chan*
_khan_search_chan(u3_khan* kan_u, c3_l sev_l, c3_l coq_l)
{
  u3_chan* ret_u;

  for ( ret_u = kan_u->san_u->can_u;
        ret_u;
        ret_u = (u3_chan*)ret_u->mor_u.nex_u ) {
    if ( coq_l == ret_u->coq_l ) {
      return ret_u;
    }
  }
  return 0;
}

/* _khan_ef_handle(): handle result.
*/
static void
_khan_ef_handle(u3_khan*  kan_u,
                c3_l      sev_l,
                c3_l      coq_l,
                c3_l      rid_l,
                u3_noun   tag,
                u3_noun   dat)
{
  u3_chan* can_u;

  //  TODO: socket events (close connection; any others?)
  //

  if ( 0 != (can_u = _khan_search_chan(kan_u, sev_l, coq_l)) ) {
    if ( c3__avow == tag ) {
      _khan_send_noun(can_u, u3nc(rid_l, u3k(dat)));
    }
    else {
      //  TODO u3_king_bail? silently drop it?
      //
      can_u->mor_u.bal_f(can_u, -1, "handle-unknown");
    }
  }
  else {
    u3l_log("khan: handle-no-coq %" PRIx32 " %" PRIu32 "\n",
            sev_l, coq_l);
  }
  u3z(tag); u3z(dat);
}

/* _khan_io_kick(): apply effects.
*/
static c3_o
_khan_io_kick(u3_auto* car_u, u3_noun wir, u3_noun cad)
{
  u3_khan* kan_u = (u3_khan*)car_u;

  u3_noun tag, dat, i_wir, t_wir;
  c3_o ret_o;

  if (  (c3n == u3r_cell(wir, &i_wir, &t_wir))
     || (c3n == u3r_cell(cad, &tag, &dat))
     || (c3__khan != i_wir) )
  {
    u3z(wir); u3z(cad);
    return c3n;
  }
  else {
    u3_noun pud = t_wir;
    u3_noun p_pud, t_pud, tt_pud, q_pud, r_pud, s_pud;
    c3_l    sev_l, coq_l, rid_l;

    if ( (c3n == u3r_cell(pud, &p_pud, &t_pud)) ||
         (c3n == u3_reck_lily(c3__uv, u3k(p_pud), &sev_l)) ||
         sev_l != kan_u->sev_l )
    {
      u3z(wir); u3z(cad);
      return c3n;
    }

    if ( u3_nul == t_pud ) {
      coq_l = rid_l = 0;
    }
    else {
      if ( (c3n == u3r_cell(t_pud, &q_pud, &tt_pud)) ||
           (c3n == u3_reck_lily(c3__ud, u3k(q_pud), &coq_l)) )
      {
        u3z(wir); u3z(cad);
        return c3n;
      }

      if ( u3_nul == tt_pud ) {
        rid_l = 0;
      }
      else {
        if ( (c3n == u3r_cell(tt_pud, &r_pud, &s_pud)) ||
             (u3_nul != s_pud) ||
             (c3n == u3_reck_lily(c3__ud, u3k(r_pud), &rid_l)) )
        {
          u3z(wir); u3z(cad);
          return c3n;
        }
      }
    }

    _khan_ef_handle(kan_u, sev_l, coq_l, rid_l, u3k(tag), u3k(dat));
    u3z(wir); u3z(cad);
    return c3y;
  }
}

/* _khan_io_exit(): unlink socket, shut down connections.
*/
static void
_khan_io_exit(u3_auto* car_u)
{
  u3_khan*          kan_u = (u3_khan*)car_u;
  c3_c*             pax_c = u3_Host.dir_c;
  c3_w              len_w = strlen(pax_c) + 1 + sizeof(URB_SOCK_PATH);
  c3_c*             paf_c = c3_malloc(len_w);
  c3_i              wit_i;

  wit_i = snprintf(paf_c, len_w, "%s/%s", pax_c, URB_SOCK_PATH);
  c3_assert(wit_i > 0);
  c3_assert(len_w == (c3_w)wit_i + 1);

  if ( 0 != unlink(paf_c) ) {
    if ( ENOENT != errno ) {
      u3l_log("khan: failed to unlink socket: %s\n", uv_strerror(errno));
    }
  }
  else {
    u3l_log("khan: unlinked %s\n", paf_c);
  }
  c3_free(paf_c);

  {
    u3_shan*        san_u = kan_u->san_u;
    u3_chan*        can_u = san_u ? san_u->can_u : 0;
    u3_chan*        nex_u;

    while ( can_u ) {
      nex_u = (u3_chan*)can_u->mor_u.nex_u;
      _khan_close_socket(kan_u, can_u);
      can_u = nex_u;
    }
    if ( san_u ) {
      uv_close((uv_handle_t*)&san_u->pyp_u, _khan_close_cb);
    }
  }

  u3s_cue_xeno_done(kan_u->sil_u);
  c3_free(kan_u);
}

/* u3_khan(): initialize control plane socket.
*/
u3_auto*
u3_khan_io_init(u3_pier* pir_u)
{
  u3_khan* kan_u = c3_calloc(sizeof(*kan_u));
  u3_auto* car_u = &kan_u->car_u;

  kan_u->sil_u = u3s_cue_xeno_init();
  car_u->nam_m = c3__khan;
  car_u->liv_o = c3n;
  car_u->io.talk_f = _khan_io_talk;
  car_u->io.kick_f = _khan_io_kick;
  car_u->io.exit_f = _khan_io_exit;

  {
    u3_noun         now;
    struct timeval  tim_u;

    gettimeofday(&tim_u, 0);
    now = u3_time_in_tv(&tim_u);
    kan_u->sev_l = u3r_mug(now);
    u3z(now);
  }

  return car_u;
}

#endif  //  _WIN32
