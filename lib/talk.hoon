::
::::  /hoon/talk/lib
  ::
  ::  This file is in the public domain.
  ::
/?    310
/-    talk
::
::::
  ::
[. ^talk]
|_  bol/bowl:gall
::
::TODO  add to zuse?
++  true-self
  |=  who/ship
  ?.  ?=($earl (clan:title who))  who
  (sein:title who)
::
++  above
  |=  who/ship
  ?:  ?=($czar (clan:title who))  ~zod
  (sein:title who)
::
++  said-url                                            ::  app url
  |=  url/purl:eyre
  :^  ost.bol  %poke  /said-url
  :+  [our.bol %talk]  %talk-action
  ^-  action
  :+  %phrase
    [[our.bol %inbox] ~ ~]
  [%app dap.bol (crip (en-purl:html url))]~   :: XX
::
++  said                                                ::  app message
  |=  {our/@p dap/term now/@da eny/@uvJ mes/(list tank)}
  :-  %talk-action
  ^-  action
  :-  %convey
  |-  ^-  (list thought)
  ?~  mes  ~
  :_  $(mes t.mes, eny (sham eny mes))
  ^-  thought
  :+  (shaf %thot eny)
    [[our %inbox] ~ ~]
  [now [%app dap (crip ~(ram re i.mes))]]
::
++  uniq
  |=  eny/@uvJ
  ^-  {serial _eny}
  [(shaf %serial eny) (shax eny)]
::
++  range-to-path                                       ::<  msg range to path
  ::>  turns a range structure into a path used for
  ::>  subscriptions.
  ::
  |=  ran/range
  ^-  path
  ?~  ran  ~
  %+  welp
    /(scot -.hed.u.ran +.hed.u.ran)
  ?~  tal.u.ran  ~
  /(scot -.u.tal.u.ran +.u.tal.u.ran)
::
++  path-to-range                                       ::<  path to msg range
  ::>  turns the tail of a subscription path into a
  ::>  range structure.
  ::
  |=  pax/path
  ^-  range
  ?~  pax  ~
  :+  ~
    =+  hed=(slaw %da i.pax)
    ?^  hed  [%da u.hed]
    [%ud (slav %ud i.pax)]
  ?~  t.pax  ~
  :-  ~
  =+  tal=(slaw %da i.t.pax)
  ?^  tal  [%da u.tal]
  [%ud (slav %ud i.t.pax)]
::
++  change-glyphs                                       ::<  ...
  ::>
  ::
  |=  {gys/(jug char audience) bin/? gyf/char aud/audience}
  ^+  gys
  ::  simple bind.
  ?:  bin  (~(put ju gys) gyf aud)
  ::  unbind all of glyph.
  ?~  aud  (~(del by gys) gyf)
  ::  unbind single.
  (~(del ju gys) gyf aud)
::
++  change-nicks                                        ::<  change nick map
  ::>  changes a nickname in a map, adding if it doesn't
  ::>  yet exist, removing if the nickname is empty.
  ::
  |=  {nis/(map ship cord) who/ship nic/cord}
  ^+  nis
  ?:  =(nic '')
    (~(del by nis) who)
  (~(put by nis) who nic)
::
++  change-config                                       ::<  apply config diff
  ::>  applies a config diff to the given config.
  ::
  |=  {cof/config dif/diff-config}
  ^+  cof
  ?-  -.dif
    $full     cof.dif
    $caption  cof(cap cap.dif)
    $filter   cof(fit fit.dif)
    $remove   cof
    ::
      $source
    %=  cof
        src
      %.  src.dif
      ?:  add.dif
        ~(put in src.cof)
      ~(del in src.cof)
    ==
    ::
      $permit
    %=  cof
        sis.con
      %.  sis.dif
      ?:  add.dif
        ~(uni in sis.con.cof)
      ~(dif in sis.con.cof)
    ==
    ::
      $secure
    %=  cof
        sec.con
      sec.dif
      ::
        sis.con
      ?.  .=  ?=(?($white $green) sec.dif)
              ?=(?($white $green) sec.con.cof)
        ~
      sis.con.cof
    ==
  ==
::
++  change-status                                       ::<  apply status diff
  ::>  applies a status diff to the given status.
  ::
  |=  {sat/status dif/diff-status}
  ^+  sat
  ?-  -.dif
    $full       sat.dif
    $presence   sat(pec pec.dif)
    $remove     sat
    ::
      $human
    %=  sat
        man
      ?-  -.dif.dif
        $full     man.dif.dif
        $true     [han.man.sat tru.dif.dif]
        $handle   [han.dif.dif tru.man.sat]
      ==
    ==
  ==
::
::TODO  annotate all!
++  depa                                              ::  de-pathing core
  =>  |%  ++  grub  *                                 ::  result
          ++  weir  (list coin)                       ::  parsed wire
          ++  fist  $-(weir grub)                     ::  reparser instance
      --
  |%
  ::
  ++  al
    |*  {hed/$-(coin *) tal/fist}
    |=  wir/weir  ^+  [*hed *tal]
    ?~  wir  !!
    [(hed i.wir) (tal t.wir)]
  ::
  ++  at
    |*  typ/{@tas (pole @tas)}
    =+  [i-typ t-typ]=typ
    |=  wer/weir
    ^-  (tup:dray:wired i-typ t-typ)  ::< ie, (tup %p %tas ~) is {@p @tas}
    ?~  wer  !!
    ?~  t-typ
      ?^  t.wer  !!
      ((do i-typ) i.wer)
    :-  ((do i-typ) i.wer)
    (^$(typ t-typ) t.wer)
  ::
  ++  mu                                              ::  true unit
    |*  wit/fist
    |=  wer/weir
    ?~(wer ~ (some (wit wer)))
  ::
  ++  af                                              ::  object as frond
    |*  buk/(pole {cord fist})
    |=  wer/weir
    ?>  ?=({{$$ $tas @tas} *} wer)
    ?~  buk  !!
    =+  [[tag wit] t-buk]=buk
    ?:  =(tag q.p.i.wer)
      [tag ~|(tag+`@tas`tag (wit t.wer))]
    ?~  t-buk  ~|(bad-tag+`@tas`q.p.i.wer !!)
    (^$(buk t-buk) wer)
  ::
  ++  or
    =+  tmp=|-($@(@tas {@tas $}))  ::TODO typ/that syntax-errors...
    |*  typ/tmp
    |=  con/coin
    ::^-  _(snag *@ (turn (limo typ) |*(a/@tas [a (odo:raid:wired a)])))
    ?>  ?=($$ -.con)
    =/  i-typ  ?@(typ typ -.typ)
    ?:  =(i-typ p.p.con)
      :-  i-typ
      ^-  (odo:raid:wired i-typ)
      q.p.con
    ?@  typ  ~|(%bad-odor !!)
    (^$(typ +.typ) con)
  ::
  ++  do
    |*  typ/@tas
    =/  typecheck  `@tas`typ
    |=  con/coin
    ^-  (odo:raid:wired typ)
    ?.  ?=($$ -.con)  ~|(%not-dime !!)
    ?.  =(typ p.p.con)  ~|(bad-odor+`@tas`p.p.con !!)
    q.p.con
  ::
  ++  ul                                              ::  null
    |=(wer/weir ?~(wer ~ !!))
  ::
  ++  un
    |*  wit/$-(coin *)
    |=  wer/weir  ^+  *wit
    ?~  wer  !!
    ?^  t.wer  !!
    (wit i.wer)
  --
--
