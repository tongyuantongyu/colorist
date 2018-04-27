# ---------------------------------------------------------------------------
#                         Copyright Joe Drago 2018.
#         Distributed under the Boost Software License, Version 1.0.
#            (See accompanying file LICENSE_1_0.txt or copy at
#                  http://www.boost.org/LICENSE_1_0.txt)
# ---------------------------------------------------------------------------

Number.prototype.clamp = (min, max) ->
  return Math.min(Math.max(this, min), max)

module.exports =
  fr: (number, digits) ->
    s = number.toFixed(digits)
    s = s.replace(/0$/g, "")
    return s
  clamp: (v, min, max) ->
    return Math.min(Math.max(v, min), max)
